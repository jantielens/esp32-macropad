#include "alarm_manager.h"
#if ALARM_ENABLED
#include "action_parse.h"
#include "action_registry.h"
#include "action_validate.h"
#include "action_dispatch.h"
#include "config_psram.h"
#include "psram_json_allocator.h"
#include "storage.h"
#include "time_service.h"
#include "ota_activity.h"
#include "log_manager.h"
#include <memory>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <string.h>
#if defined(ARDUINO)
#include "display_manager.h"
#include <esp_timer.h>
#endif

static constexpr size_t ALARM_JSON_MAX = 8192;
static constexpr size_t ALARM_SNAPSHOT_MAX = 16384;
static const char* const snapshot_paths[] = {"/config/alarm_a.json", "/config/alarm_b.json"};
static std::mutex alarm_mutex;
static AlarmDefinition definition;
static AlarmDefinition session;
static AlarmClockCore clock_core;
static uint64_t generation = 0;
static time_t once_epoch = 0;
static uint32_t timezone_revision = 0;
static int active_snapshot = -1;
static bool initialized = false;
static bool storage_error = false;
static std::atomic<bool> hook_error{false};
static char last_error[128] = {};
struct AlarmCommand { uint8_t operation; int value; uint8_t day; };
static AlarmCommand commands[4] = {};
static uint8_t command_count = 0;
static bool command_processing = false;
static bool command_error = false;
static char command_message[128] = {};
static uint32_t completed_commands = 0;
static constexpr uint64_t SETTINGS_SAVE_DELAY_MS = 10000;
static bool settings_dirty = false;
static bool settings_save_failed = false;
static uint64_t settings_save_due_ms = 0;
static time_t next_epoch = 0;
static time_t forecast_minute = -1;
static ButtonAction pending_stop[MAX_BUTTON_ACTIONS] = {};
static uint8_t stop_count = 0;
static uint8_t stop_index = 0;
static uint8_t ring_index = 0;
static bool ring_pending = false;
static bool session_started = false;
static bool audio_failed = false;
static time_t ring_until_epoch = 0;
static uint64_t ring_until_ms = 0;
static std::atomic<uint32_t> session_generation{1};
static uint64_t last_schedule_log_ms = 0;
static bool schedule_log_pending = true;
static bool logged_ota_active = false;

static bool session_work_valid(uint32_t value) { return value == session_generation.load(); }
static bool stop_work_valid(uint32_t value) { return session_work_valid(value); }

using AlarmDocument = BasicJsonDocument<PsramJsonAllocator>;
using DefinitionPtr = std::unique_ptr<AlarmDefinition, decltype(&free)>;

static bool fail(const char* message) {
    strlcpy(last_error, message, sizeof(last_error));
    LOGW("Alarm", "Operation failed: %s", message);
    return false;
}

static const char* state_name(AlarmState state) {
    return state == ALARM_RINGING ? "ringing" : state == ALARM_SNOOZED ? "snoozed" : "idle";
}

static void log_definition(const char* reason) {
    LOGI("Alarm", "%s: slot=1 enabled=%u time=%02u:%02u weekdays=0x%02x ring_hooks=%u stop_hooks=%u snooze=%umin dismiss=%umin",
        reason, unsigned(definition.enabled), unsigned(definition.hour), unsigned(definition.minute),
        unsigned(definition.weekdays), unsigned(definition.ring_count), unsigned(definition.stop_count),
        unsigned(definition.snooze_minutes), unsigned(definition.dismiss_minutes));
    LOGI("Alarm", "Armed: mode=%s ready=%u from=%lld handled=%lld once=%lld", definition.weekdays ? "weekly" : "once", unsigned(clock_core.time_ready),
        static_cast<long long>(clock_core.armed_from), static_cast<long long>(clock_core.handled_epoch), static_cast<long long>(once_epoch));
}

static void log_schedule(time_t now, time_t candidate, bool ready, bool ota, bool rearmed) {
    struct tm local = {};
    char local_time[32] = "unavailable";
    if (time_service_localtime(now, &local)) strftime(local_time, sizeof(local_time), "%Y-%m-%d %H:%M:%S", &local);
    const char* blocked = !definition.enabled ? "disabled"
        : !ready ? "waiting-ntp" : !clock_core.time_ready ? "arming-after-sync" : ota ? "ota"
        : rearmed ? "rearmed" : stop_index < stop_count ? "cleanup-pending" : !candidate ? "not-due"
        : candidate < clock_core.armed_from ? "before-armed-minute"
        : candidate <= clock_core.handled_epoch ? "already-handled"
        : candidate > now ? "not-due" : now - candidate > clock_core.lateness_seconds ? "outside-grace" : "none";
    LOGD("Alarm", "Schedule: local=%s day=%u set=%02u:%02u weekdays=0x%02x ready=%u ota=%u state=%s candidate=%lld armed=%lld handled=%lld blocked=%s",
        local_time, unsigned(local.tm_wday), unsigned(definition.hour), unsigned(definition.minute),
        unsigned(definition.weekdays), unsigned(ready), unsigned(ota), state_name(clock_core.state),
        static_cast<long long>(candidate), static_cast<long long>(clock_core.armed_from),
        static_cast<long long>(clock_core.handled_epoch), blocked);
}

static uint32_t checksum(const std::string& text) {
    uint32_t crc = UINT32_MAX;
    for (size_t index = 0; index < text.length(); ++index) {
        crc ^= static_cast<uint8_t>(text[index]);
        for (uint8_t bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

static void definition_json(const AlarmDefinition& value, JsonObject root) {
    root["lateness_minutes"] = value.lateness_minutes;
    JsonObject slot = root.createNestedObject("1");
    slot["enabled"] = value.enabled;
    slot["hour"] = value.hour;
    slot["minute"] = value.minute;
    slot["weekdays"] = value.weekdays;
    slot["snooze_minutes"] = value.snooze_minutes;
    slot["auto_dismiss_minutes"] = value.dismiss_minutes;
    JsonArray ring = slot.createNestedArray("on_ring");
    JsonArray stop = slot.createNestedArray("on_stop");
    for (uint8_t index = 0; index < value.ring_count; ++index)
        action_to_json(value.on_ring[index], ring.createNestedObject());
    for (uint8_t index = 0; index < value.stop_count; ++index)
        action_to_json(value.on_stop[index], stop.createNestedObject());
}

static bool parse_hooks(JsonVariantConst input, ButtonAction* output, uint8_t* count) {
    if (!input.is<JsonArrayConst>() || input.size() > MAX_BUTTON_ACTIONS)
        return fail("Hook must be an array of at most three actions");
    for (JsonVariantConst item : input.as<JsonArrayConst>()) {
        if (!item.is<JsonObjectConst>()) return fail("Hook action must be an object");
        JsonObjectConst action = item.as<JsonObjectConst>();
        const char* type_name = action["type"] | "";
        const ActionTypeDef* type = action_type_find(type_name);
        if (!type || !action_type_is_supported(type_name)) return fail("Hook action is unavailable");
        if (type->execution != ACTION_EXECUTION_SYNC) return fail("Pausable or unclassified actions are not allowed in alarm hooks");
        const char* error = action_validate_json(action);
        if (error) return fail(error);
        JsonDocument copy;
        copy.set(action);
        action_parse(copy.as<JsonObject>(), output[*count]);
        if (!output[*count].type[0]) return fail("Invalid or oversized hook action");
        JsonDocument normalized;
        action_to_json(output[*count], normalized.to<JsonObject>());
        for (JsonPairConst field : action) {
            JsonVariantConst stored = normalized[field.key().c_str()];
            if (field.value().is<const char*>()) {
                const char* input = field.value().as<const char*>();
                if ((!stored.is<const char*>() && input[0])
                    || (stored.is<const char*>() && strcmp(input, stored.as<const char*>())))
                    return fail("Hook string field is invalid or oversized");
            }
        }
        ++*count;
    }
    return true;
}

static bool parse_definition(JsonObjectConst root, AlarmDefinition* output) {
    if (root.size() != 2 || !root["1"].is<JsonObjectConst>()) return fail("Alarm config requires lateness_minutes and exactly slot 1");
    if (!root["lateness_minutes"].is<int>() || root["lateness_minutes"].as<int>() < 0
        || root["lateness_minutes"].as<int>() > 10080) return fail("lateness_minutes must be 0-10080 whole minutes");
    output->lateness_minutes = root["lateness_minutes"];
    JsonObjectConst slot = root["1"].as<JsonObjectConst>();
    for (JsonPairConst field : slot) {
        const char* name = field.key().c_str();
        if (strcmp(name, "enabled") && strcmp(name, "hour") && strcmp(name, "minute")
            && strcmp(name, "weekdays") && strcmp(name, "snooze_minutes")
            && strcmp(name, "auto_dismiss_minutes") && strcmp(name, "on_ring") && strcmp(name, "on_stop"))
            return fail("Unknown alarm configuration field");
    }
    if (!slot["enabled"].is<bool>()) return fail("enabled must be boolean");
    const char* numeric[] = {"hour", "minute", "weekdays", "snooze_minutes", "auto_dismiss_minutes"};
    const int limits[] = {23, 59, 127, 1440, 1440};
    for (uint8_t index = 0; index < 5; ++index) {
        if (!slot[numeric[index]].is<int>() || slot[numeric[index]].as<int>() > limits[index]
            || slot[numeric[index]].as<int>() < (index >= 3 ? 1 : 0))
            return fail("Invalid alarm time, weekdays, or duration (durations: 1-1440 minutes)");
    }
    output->enabled = slot["enabled"];
    output->hour = slot["hour"];
    output->minute = slot["minute"];
    output->weekdays = slot["weekdays"];
    output->snooze_minutes = slot["snooze_minutes"];
    output->dismiss_minutes = slot["auto_dismiss_minutes"];
    return parse_hooks(slot["on_ring"], output->on_ring, &output->ring_count)
        && parse_hooks(slot["on_stop"], output->on_stop, &output->stop_count);
}

static bool read_snapshot(int index, AlarmDocument& document) {
    File file = Storage.open(snapshot_paths[index], "r");
    if (!file || !file.size() || file.size() > ALARM_SNAPSHOT_MAX) { file.close(); return false; }
    const bool parsed = !deserializeJson(document, file);
    file.close();
    if (!parsed || document["schema"] != 3 || !document["generation"].is<uint64_t>()
        || !document["data"].is<JsonObject>()) return false;
    const uint32_t stored_checksum = document["checksum"].as<uint32_t>();
    if (!document["checksum"].is<uint32_t>()) return false;
    document.remove("checksum");
    std::string payload;
    serializeJson(document, payload);
    return payload.length() <= ALARM_SNAPSHOT_MAX && checksum(payload) == stored_checksum;
}

static bool persist(const AlarmDefinition& candidate, time_t handled, time_t once, time_t armed = clock_core.armed_from) {
    settings_save_failed = settings_dirty;
    if (ota_activity_is_active()) return fail("Firmware update in progress");
    if (generation == UINT64_MAX) return fail("Alarm snapshot generation exhausted");
    AlarmDocument document(ALARM_SNAPSHOT_MAX);
    document["schema"] = 3;
    document["generation"] = generation + 1;
    JsonObject data = document.createNestedObject("data");
    definition_json(candidate, data.createNestedObject("config"));
    data["handled_epoch"] = static_cast<int64_t>(handled);
    data["once_epoch"] = static_cast<int64_t>(once);
    data["armed_from"] = static_cast<int64_t>(armed);
    char timezone[64];
    time_service_get_timezone(timezone, sizeof(timezone));
    data["timezone"] = timezone;
    std::string payload;
    serializeJson(document, payload);
    document["checksum"] = checksum(payload);
    if (document.overflowed() || payload.length() > ALARM_JSON_MAX) return fail("Alarm snapshot exceeds capacity");
    const int destination = active_snapshot == 0 ? 1 : 0;
    File file = Storage.open(snapshot_paths[destination], "w");
    if (!file) return fail("Cannot open alarm snapshot");
    const size_t expected = measureJson(document);
    const size_t written = serializeJson(document, file);
    file.flush();
    file.close();
    if (written != expected) return fail("Incomplete alarm snapshot write");
    document.clear();
    if (!read_snapshot(destination, document) || document["generation"].as<uint64_t>() != generation + 1)
        return fail("Alarm snapshot verification failed");
    active_snapshot = destination;
    ++generation;
    settings_dirty = settings_save_failed = false;
    storage_error = false;
    last_error[0] = '\0';
    return true;
}

static uint8_t dispatch_hooks(const ButtonAction* actions, uint8_t count, const char* label, uint8_t index,
                              bool (*guard)(uint32_t) = session_work_valid) {
    for (; index < count; ++index) {
        if (ota_activity_is_active()) break;
        LOGI("Alarm", "%s hook %u/%u: dispatch type=%s", label, unsigned(index + 1), unsigned(count), actions[index].type);
        const ActionResult result = action_dispatch_synchronous(actions[index], label, guard, session_generation.load());
        if (result != ACTION_COMPLETE) {
            hook_error = true;
            LOGW("Alarm", "%s hook %u/%u: failed result=%u", label, unsigned(index + 1), unsigned(count), unsigned(result));
        } else {
            LOGI("Alarm", "%s hook %u/%u: dispatch complete", label, unsigned(index + 1), unsigned(count));
        }
    }
    return index;
}

static void stop_session() {
    session_generation.fetch_add(1);
    audio_failed = false;
    if (session_started) {
        memcpy(pending_stop, session.on_stop, sizeof(pending_stop));
        stop_count = session.stop_count;
        stop_index = 0;
    }
    session_started = false;
    ring_pending = false;
}

static void flush_hooks() {
    const uint8_t stopped = dispatch_hooks(pending_stop, stop_count, "Alarm stop", stop_index, stop_work_valid);
    {
        std::lock_guard<std::mutex> lock(alarm_mutex);
        stop_index = stopped;
    }
    if (stopped < stop_count || !ring_pending || ota_activity_is_active()) return;
    const uint8_t next = dispatch_hooks(session.on_ring, session.ring_count, "Alarm ring", ring_index);
    std::lock_guard<std::mutex> lock(alarm_mutex);
    if (next > ring_index || !session.ring_count) session_started = true;
    ring_index = next;
    if (ring_index == session.ring_count) ring_pending = false;
}

static uint64_t monotonic_ms() {
#if defined(ARDUINO)
    return static_cast<uint64_t>(esp_timer_get_time()) / 1000;
#else
    return millis();
#endif
}

static void update_forecast(time_t now) {
    forecast_minute = now / 60;
    next_epoch = 0;
    if (!definition.enabled || !time_service_ready()) return;
    if (!definition.weekdays) {
        if (once_epoch >= clock_core.armed_from && once_epoch > clock_core.handled_epoch && once_epoch >= now)
            next_epoch = once_epoch;
        return;
    }
    const time_t floor = std::max(now - 1, std::max(clock_core.armed_from - 1, clock_core.handled_epoch));
    next_epoch = time_service_alarm_next(floor, definition.hour, definition.minute, definition.weekdays);
}

void alarm_manager_init() {
    std::lock_guard<std::mutex> lock(alarm_mutex);
    definition = AlarmDefinition{};
    session = AlarmDefinition{};
    clock_core = AlarmClockCore{};
    generation = 0;
    once_epoch = 0;
    active_snapshot = -1;
    command_count = 0;
    command_processing = command_error = false;
    command_message[0] = '\0';
    completed_commands = 0;
    settings_dirty = settings_save_failed = false;
    settings_save_due_ms = 0;
    next_epoch = 0;
    forecast_minute = -1;
    storage_error = false;
    hook_error = false;
    initialized = false;
    session_generation.fetch_add(1);
    last_error[0] = '\0';
    stop_count = stop_index = ring_index = 0;
    ring_pending = session_started = false;
    audio_failed = false;
    last_schedule_log_ms = 0;
    schedule_log_pending = true;
    logged_ota_active = ota_activity_is_active();
    AlarmDocument document(ALARM_SNAPSHOT_MAX);
    DefinitionPtr candidate(static_cast<AlarmDefinition*>(config_psram_alloc(sizeof(AlarmDefinition), "alarm_load")), free);
    if (!candidate) { storage_error = true; fail("Alarm allocation failed"); return; }
    bool invalid_snapshot = false;
    char saved_timezone[64] = {};
    for (int index = 0; index < 2; ++index) {
        document.clear();
        if (!read_snapshot(index, document)) {
            invalid_snapshot |= Storage.exists(snapshot_paths[index]);
            continue;
        }
        *candidate = AlarmDefinition{};
        if (!parse_definition(document["data"]["config"], candidate.get())
            || !document["data"]["handled_epoch"].is<int64_t>()
            || document["data"]["handled_epoch"].as<int64_t>() < 0
            || !document["data"]["once_epoch"].is<int64_t>()
            || document["data"]["once_epoch"].as<int64_t>() < 0
            || !document["data"]["armed_from"].is<int64_t>()
            || document["data"]["armed_from"].as<int64_t>() < -1
            || !document["data"]["timezone"].is<const char*>()
            || strlen(document["data"]["timezone"].as<const char*>()) >= sizeof(saved_timezone)
            || ((!candidate->enabled || candidate->weekdays) && document["data"]["once_epoch"].as<int64_t>() != 0)) { invalid_snapshot = true; continue; }
        const uint64_t stored_generation = document["generation"].as<uint64_t>();
        if (active_snapshot < 0 || stored_generation > generation) {
            definition = *candidate;
            clock_core.handled_epoch = document["data"]["handled_epoch"].as<int64_t>();
            once_epoch = document["data"]["once_epoch"].as<int64_t>();
            clock_core.armed_from = document["data"]["armed_from"].as<int64_t>();
            strlcpy(saved_timezone, document["data"]["timezone"], sizeof(saved_timezone));
            generation = stored_generation;
            active_snapshot = index;
        }
    }
    if (invalid_snapshot) {
        storage_error = true;
        fail(active_snapshot < 0 ? "No valid alarm snapshot; alarm disabled" : "Recovered alarm snapshot; storage degraded");
    }
    timezone_revision = time_service_generation();
    clock_core.time_ready = time_service_ready();
    clock_core.lateness_seconds = definition.lateness_minutes * 60U;
    char current_timezone[64];
    time_service_get_timezone(current_timezone, sizeof(current_timezone));
    if (active_snapshot >= 0 && strcmp(saved_timezone, current_timezone)) {
        clock_core.rearm(time(nullptr), time_service_ready());
        settings_dirty = true;
        settings_save_due_ms = 0;
    }
    initialized = true;
    update_forecast(time(nullptr));
    LOGI("Alarm", "Initialized: snapshot=%d generation=%llu storage_error=%u", active_snapshot,
        static_cast<unsigned long long>(generation), unsigned(storage_error));
    log_definition("Loaded");
}

static bool alarm_config_apply_raw(const uint8_t* data, size_t length, bool deferred_save) {
    if (!initialized || !data || !length || length > ALARM_JSON_MAX) return fail("Alarm configuration unavailable or oversized");
    if (ota_activity_is_active()) return fail("Firmware update in progress");
    DefinitionPtr candidate(static_cast<AlarmDefinition*>(config_psram_alloc(sizeof(AlarmDefinition), "alarm_candidate")), free);
    if (!candidate) return fail("Alarm allocation failed");
    *candidate = AlarmDefinition{};
    AlarmDocument document(ALARM_JSON_MAX * 2);
    if (deserializeJson(document, data, length) || !document.is<JsonObject>()) return fail("Invalid alarm JSON");
    if (!parse_definition(document.as<JsonObjectConst>(), candidate.get())) return false;
    {
        std::lock_guard<std::mutex> lock(alarm_mutex);
        AlarmDocument current(ALARM_JSON_MAX);
        definition_json(definition, current.to<JsonObject>());
        std::string before;
        serializeJson(current, before);
        current.clear();
        definition_json(*candidate, current.to<JsonObject>());
        std::string after;
        serializeJson(current, after);
        current["lateness_minutes"] = definition.lateness_minutes;
        std::string schedule;
        serializeJson(current, schedule);
        const bool schedule_changed = before != schedule;
        if (before == after) {
            if (!deferred_save && settings_dirty && !persist(definition, clock_core.handled_epoch, once_epoch)) {
                storage_error = settings_save_failed = true;
                return false;
            }
            LOGI("Alarm", "Configuration unchanged; session preserved (%s)", state_name(clock_core.state));
            return true;
        }
        if (stop_index < stop_count) return fail("Alarm cleanup pending");
        const time_t armed = schedule_changed ? (time_service_ready() ? (time(nullptr) / 60 + 1) * 60 : -1) : clock_core.armed_from;
        const time_t next_once = !schedule_changed ? once_epoch : candidate->enabled && !candidate->weekdays && time_service_ready()
            ? time_service_alarm_next(time(nullptr), candidate->hour, candidate->minute) : 0;
        if (candidate->enabled && !candidate->weekdays && time_service_ready() && !next_once)
            return fail("Cannot schedule one-shot alarm");
        if (!deferred_save && !persist(*candidate, clock_core.handled_epoch, next_once, armed)) {
            storage_error = true;
            settings_save_failed = settings_dirty;
            return false;
        }
        if (schedule_changed) {
            clock_core.cancel();
            stop_session();
            clock_core.rearm(time(nullptr), time_service_ready());
        }
        definition = *candidate;
        once_epoch = next_once;
        clock_core.lateness_seconds = definition.lateness_minutes * 60U;
        update_forecast(time(nullptr));
        schedule_log_pending = true;
        if (deferred_save) {
            settings_dirty = true;
            settings_save_due_ms = monotonic_ms() + SETTINGS_SAVE_DELAY_MS;
        }
        log_definition(deferred_save ? "Configuration applied; save pending" : "Configuration saved");
    }
    flush_hooks();
    return true;
}

bool alarm_config_save_raw(const uint8_t* data, size_t length) {
    return alarm_config_apply_raw(data, length, false);
}

bool alarm_config_save_section_raw(const uint8_t* data, size_t length, AlarmConfigSection section) {
    AlarmDocument input(ALARM_JSON_MAX * 2);
    if (deserializeJson(input, data, length) || !input.is<JsonObject>()) return fail("Invalid alarm JSON");
    const char* schedule_fields[] = {"enabled", "hour", "minute", "weekdays"};
    const char* behavior_fields[] = {"lateness_minutes", "snooze_minutes", "auto_dismiss_minutes", "on_ring", "on_stop"};
    const bool behavior = section == AlarmConfigSection::Behavior;
    const char* const* fields = behavior ? behavior_fields : schedule_fields;
    const size_t count = behavior ? 5 : 4;
    if (input.size() != count) return fail("Alarm section requires exactly its own fields");
    AlarmDocument merged(ALARM_JSON_MAX * 2);
    alarm_config_to_json(merged.to<JsonObject>());
    for (size_t index = 0; index < count; ++index) {
        const char* name = fields[index];
        if (!input.containsKey(name)) return fail("Missing alarm section field");
        if (!strcmp(name, "lateness_minutes")) merged[name].set(input[name]);
        else merged["1"][name].set(input[name]);
    }
    if (merged.overflowed()) return fail("Alarm allocation failed");
    std::string payload;
    serializeJson(merged, payload);
    return alarm_config_save_raw(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());
}

const char* alarm_last_error() { return last_error; }

void alarm_config_to_json(JsonObject root) {
    std::lock_guard<std::mutex> lock(alarm_mutex);
    definition_json(definition, root);
}

AlarmSnapshot alarm_snapshot() {
    std::lock_guard<std::mutex> lock(alarm_mutex);
    AlarmSnapshot value = {definition.enabled, definition.hour, definition.minute, clock_core.state,
            initialized && time_service_ready(), clock_core.deferred || ring_pending || stop_index < stop_count, storage_error, hook_error.load(), once_epoch};
    value.weekdays = definition.weekdays;
    value.lateness_minutes = definition.lateness_minutes;
    value.snooze_minutes = definition.snooze_minutes;
    value.dismiss_minutes = definition.dismiss_minutes;
    value.next_epoch = value.ready && definition.enabled ? next_epoch : 0;
    value.next_seconds = value.next_epoch ? std::max<time_t>(0, value.next_epoch - time(nullptr)) : 0;
    const uint64_t now = monotonic_ms();
    const uint32_t remaining = clock_core.deadline_ms > now ? (clock_core.deadline_ms - now + 999) / 1000 : 0;
    if (clock_core.state == ALARM_SNOOZED) value.snooze_remaining = remaining;
    if (clock_core.state == ALARM_RINGING) value.dismiss_remaining = remaining;
    value.next_ring_seconds = value.state == ALARM_RINGING ? 0
        : value.state == ALARM_SNOOZED ? (value.next_epoch ? std::min(value.snooze_remaining, value.next_seconds) : value.snooze_remaining)
        : value.next_seconds;
    value.pending_commands = command_count + command_processing;
    value.completed_commands = completed_commands;
    value.command_error = command_error || settings_save_failed;
    strlcpy(value.command_message, settings_save_failed ? last_error : command_message, sizeof(value.command_message));
    value.save_pending = settings_dirty;
    value.save_failed = settings_save_failed;
    return value;
}

void alarm_status_to_json(JsonObject root) {
    AlarmSnapshot value = alarm_snapshot();
    root["active_id"] = value.state == ALARM_IDLE ? 0 : 1;
    root["state"] = value.state == ALARM_RINGING ? "ringing" : value.state == ALARM_SNOOZED ? "snoozed" : "idle";
    root["ready"] = value.ready;
    root["ota_deferred"] = value.deferred;
    root["storage_error"] = value.storage_error;
    root["hook_error"] = value.hook_error;
    root["enabled"] = value.enabled;
    root["lateness_minutes"] = value.lateness_minutes;
    root["once_epoch"] = static_cast<int64_t>(value.once_epoch);
    char local[32] = {};
    if (value.once_epoch) time_service_format(value.once_epoch, "%Y-%m-%d %H:%M", nullptr, local, sizeof(local));
    root["once_local"] = local;
    root["once_available"] = value.once_epoch != 0;
    root["hour"] = value.hour;
    root["minute"] = value.minute;
    root["weekdays"] = value.weekdays;
    root["snooze_minutes"] = value.snooze_minutes;
    root["auto_dismiss_minutes"] = value.dismiss_minutes;
    root["next_epoch"] = static_cast<int64_t>(value.next_epoch);
    local[0] = '\0';
    if (value.next_epoch) time_service_format(value.next_epoch, "%Y-%m-%d %H:%M", nullptr, local, sizeof(local));
    root["next_local"] = local;
    root["next_available"] = value.next_epoch != 0;
    root["next_seconds"] = value.next_seconds;
    root["snooze_available"] = value.state == ALARM_SNOOZED;
    root["snooze_seconds"] = value.snooze_remaining;
    root["dismiss_seconds"] = value.dismiss_remaining;
    root["dismiss_available"] = value.state == ALARM_RINGING;
    root["next_ring_available"] = value.state != ALARM_IDLE || value.next_epoch != 0;
    root["next_ring_seconds"] = value.next_ring_seconds;
    root["pending_commands"] = value.pending_commands;
    root["completed_commands"] = value.completed_commands;
    root["command_error"] = value.command_error;
    root["command_message"] = value.command_message;
    root["save_state"] = value.save_state();
}

void alarm_manager_report_audio_failure(bool (*guard)(uint32_t), uint32_t work_generation) {
    std::lock_guard<std::mutex> lock(alarm_mutex);
    if (guard != session_work_valid || !session_work_valid(work_generation) || clock_core.state != ALARM_RINGING) return;
    audio_failed = true;
    hook_error = true;
}

void alarm_command_report_error(const char* message) {
    std::lock_guard<std::mutex> lock(alarm_mutex);
    command_error = true;
    strlcpy(command_message, message, sizeof(command_message));
}

bool alarm_command_submit(const char* command, uint8_t id, int value, uint8_t day) {
    std::lock_guard<std::mutex> lock(alarm_mutex);
    const char* error = alarm_command_validate(command, id, value, day);
    if (!initialized) error = "Alarm unavailable";
    if (!error && command_count == sizeof(commands) / sizeof(commands[0])) error = "Alarm command queue full";
    if (error) {
        command_error = true;
        strlcpy(command_message, error, sizeof(command_message));
        return false;
    }
    const uint8_t operation = alarm_command_operation(command);
    commands[command_count++] = {operation, value, day};
    LOGI("Alarm", "Queued command=%s id=%u state=%s", command, unsigned(id), state_name(clock_core.state));
    return true;
}

void alarm_manager_loop() {
    if (!initialized) return;
    AlarmCommand command = {};
    {
        std::lock_guard<std::mutex> lock(alarm_mutex);
        if (command_count) {
            command = commands[0];
            command_processing = true;
            --command_count;
            memmove(commands, commands + 1, command_count * sizeof(commands[0]));
        }
    }
    if (command.operation > 2) {
        AlarmDocument document(ALARM_JSON_MAX);
        alarm_config_to_json(document.to<JsonObject>());
        JsonObject slot = document["1"];
        if (command.operation == 3 || command.operation == 4) {
            const int minutes = command.operation == 3 ? command.value
                : ((slot["hour"].as<int>() * 60 + slot["minute"].as<int>() + static_cast<int64_t>(command.value)) % 1440 + 1440) % 1440;
            slot["hour"] = minutes / 60;
            slot["minute"] = minutes % 60;
        } else if (command.operation < 8) {
            slot["enabled"] = command.operation == 5 || (command.operation == 7 && !slot["enabled"].as<bool>());
        } else {
            const uint8_t mask = 1U << command.day;
            const uint8_t days = slot["weekdays"];
            slot["weekdays"] = command.operation == 8 ? days | mask
                : command.operation == 9 ? days & ~mask : days ^ mask;
        }
        std::string payload;
        serializeJson(document, payload);
        const bool saved = alarm_config_apply_raw(reinterpret_cast<const uint8_t*>(payload.data()), payload.size(), true);
        std::lock_guard<std::mutex> lock(alarm_mutex);
        command_error = !saved;
        strlcpy(command_message, saved ? "" : alarm_last_error(), sizeof(command_message));
    }
    uint8_t effects = 0;
    {
        std::lock_guard<std::mutex> lock(alarm_mutex);
        const time_t now = time(nullptr);
        const uint64_t monotonic = monotonic_ms();
        const bool ota = ota_activity_is_active();
        const bool ready = time_service_ready();
        const bool readiness_changed = ready != clock_core.time_ready;
        const bool was_deferred = clock_core.deferred;
        if (ota != logged_ota_active) {
            LOGI("Alarm", "OTA %s; pending ring=%u stop_hooks=%u", ota ? "active: hooks deferred" : "finished",
                unsigned(ring_pending), unsigned(stop_count - stop_index));
            logged_ota_active = ota;
            schedule_log_pending = true;
        }
        bool rearmed = false;
        if (ready && clock_core.armed_from < 0) {
            clock_core.rearm(now, true);
            settings_dirty = true;
            settings_save_due_ms = monotonic;
            rearmed = true;
        }
        if (timezone_revision != time_service_generation()) {
            clock_core.cancel();
            stop_session();
            clock_core.rearm(now, time_service_ready());
            timezone_revision = time_service_generation();
            settings_dirty = true;
            settings_save_due_ms = monotonic;
            rearmed = true;
            schedule_log_pending = true;
            log_definition("Timezone changed; session dismissed");
        }
        if (command.operation == 1 || command.operation == 2) {
            const bool cancel = command.operation == 1;
            const bool invalidate = cancel && (clock_core.deferred || ring_pending);
            const bool had_session = clock_core.state != ALARM_IDLE;
            const AlarmState previous_state = clock_core.state;
            const uint8_t command_effects = cancel ? clock_core.cancel() : clock_core.snooze(monotonic);
            if ((cancel && had_session) || (command_effects & ALARM_EFFECT_STOP)) stop_session();
            if (invalidate) {
                clock_core.rearm(now, time_service_ready());
                settings_dirty = true;
                settings_save_due_ms = monotonic;
            }
            rearmed = rearmed || (cancel && had_session) || invalidate || command_effects;
            LOGI("Alarm", "Command=%s: %s -> %s deadline_ms=%llu", cancel ? "cancel" : "snooze",
                state_name(previous_state), state_name(clock_core.state), static_cast<unsigned long long>(clock_core.deadline_ms));
        }
        if (audio_failed && !ota) {
            if (clock_core.snooze(monotonic) & ALARM_EFFECT_STOP) stop_session();
            LOGW("Alarm", "Audio playback failed; automatically snoozed for %umin", unsigned(session.snooze_minutes));
        }
        if (ring_pending && (now > ring_until_epoch || monotonic > ring_until_ms)) {
            LOGW("Alarm", "Pending ring expired; discarding remaining ring hooks (%u/%u dispatched)",
                unsigned(ring_index), unsigned(session.ring_count));
            clock_core.cancel();
            stop_session();
        } else if (ring_pending && !ota && !ring_index) {
            clock_core.deadline_ms = monotonic + clock_core.dismiss_ms;
        }
        bool occurrence_record_attempted = false;
        if (definition.enabled && !definition.weekdays && ready && !ota) {
            if (!once_epoch) {
                const time_t next = time_service_alarm_next(now, definition.hour, definition.minute);
                if (next) {
                    once_epoch = next;
                    if (!persist(definition, clock_core.handled_epoch, next)) {
                        storage_error = true;
                        settings_dirty = true;
                        settings_save_due_ms = monotonic + SETTINGS_SAVE_DELAY_MS;
                    }
                    schedule_log_pending = true;
                    log_definition("One-shot scheduled");
                } else storage_error = true;
            } else {
                const time_t armed = clock_core.armed_from;
                if (once_epoch < armed || once_epoch <= clock_core.handled_epoch || (once_epoch < now && now - once_epoch > clock_core.lateness_seconds)) {
                    definition.enabled = false;
                    once_epoch = 0;
                    if (persist(definition, clock_core.handled_epoch, 0)) {
                        schedule_log_pending = true;
                        log_definition("One-shot expired; disabled");
                    } else {
                        storage_error = true;
                        settings_dirty = true;
                        settings_save_due_ms = monotonic + SETTINGS_SAVE_DELAY_MS;
                    }
                }
            }
        }
        time_t candidate = definition.enabled && !rearmed && stop_index == stop_count
            ? (definition.weekdays ? time_service_alarm_candidate(now, definition.hour, definition.minute, definition.weekdays) : once_epoch) : 0;
        if (!definition.weekdays && !ota && clock_core.occurrence_due(now, ready, candidate)) {
            occurrence_record_attempted = true;
            definition.enabled = false;
            once_epoch = 0;
            if (persist(definition, candidate, 0)) {
                schedule_log_pending = true;
                LOGI("Alarm", "One-shot consumed; future scheduling disabled");
            } else {
                storage_error = true;
                settings_dirty = true;
                settings_save_due_ms = monotonic + SETTINGS_SAVE_DELAY_MS;
                LOGE("Alarm", "One-shot consumption failed; ringing with RAM-only duplicate prevention");
            }
        }
        if (schedule_log_pending || readiness_changed || (definition.enabled && monotonic - last_schedule_log_ms >= 60000)) {
            log_schedule(now, candidate, ready, ota, rearmed);
            last_schedule_log_ms = monotonic;
            schedule_log_pending = false;
        }
        const AlarmState previous_state = clock_core.state;
        uint8_t scheduled = clock_core.tick(now, monotonic, ready, ota, candidate,
            definition.snooze_minutes * 60000U, definition.dismiss_minutes * 60000U);
        if (readiness_changed) {
            LOGI("Alarm", "Time readiness=%u now=%lld armed_from=%lld; %s", unsigned(ready),
                static_cast<long long>(now), static_cast<long long>(clock_core.armed_from),
                ready ? "checking missed occurrences within lateness window" : "waiting for current-boot NTP sync");
        }
        if (clock_core.deferred != was_deferred)
            LOGI("Alarm", "Occurrence/deadline OTA deferral=%u", unsigned(clock_core.deferred));
        if (previous_state != clock_core.state && !(scheduled & ALARM_EFFECT_RING))
            LOGI("Alarm", "Deadline: %s -> %s", state_name(previous_state), state_name(clock_core.state));
        if (scheduled & ALARM_EFFECT_STOP) {
            stop_session();
        }
        effects |= scheduled;
        if (effects & ALARM_EFFECT_HANDLED) {
            if (!occurrence_record_attempted && !persist(definition, clock_core.handled_epoch, once_epoch)) {
                storage_error = true;
                settings_dirty = true;
                settings_save_due_ms = monotonic + SETTINGS_SAVE_DELAY_MS;
                LOGE("Alarm", "Occurrence recording failed; ringing with RAM-only duplicate prevention");
            }
            session = definition;
            hook_error = false;
        }
        if (effects & ALARM_EFFECT_RING) {
            LOGI("Alarm", "Ring started: reason=%s occurrence=%lld late=%llds hooks=%u stop_hooks=%u deadline_ms=%llu",
                effects & ALARM_EFFECT_HANDLED ? "schedule" : "snooze", static_cast<long long>(clock_core.handled_epoch),
                static_cast<long long>(effects & ALARM_EFFECT_HANDLED ? now - clock_core.handled_epoch : 0),
                unsigned(session.ring_count), unsigned(session.stop_count), static_cast<unsigned long long>(clock_core.deadline_ms));
            if (!session.ring_count) LOGW("Alarm", "Ring has no actions configured");
            ring_pending = true;
            ring_index = 0;
            ring_until_epoch = now + 300;
            ring_until_ms = monotonic + 300000;
        }
        if (command.operation) {
            command_processing = false;
            ++completed_commands;
            if (command.operation <= 2) {
                command_error = false;
                command_message[0] = '\0';
            }
        }
        if (forecast_minute != now / 60 || rearmed || readiness_changed || schedule_log_pending || scheduled)
            update_forecast(now);
        if (settings_dirty && monotonic >= settings_save_due_ms && !ota && !command_count) {
            if (persist(definition, clock_core.handled_epoch, once_epoch)) {
                LOGI("Alarm", "Pending settings saved");
            } else {
                storage_error = settings_save_failed = true;
                settings_save_due_ms = monotonic + SETTINGS_SAVE_DELAY_MS;
                LOGW("Alarm", "Pending settings save failed; retry in 10 seconds");
            }
        }
    }
    flush_hooks();
}
#endif