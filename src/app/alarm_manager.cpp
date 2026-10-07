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
static uint8_t commands[4] = {};
static uint8_t command_count = 0;
static ButtonAction pending_stop[MAX_BUTTON_ACTIONS] = {};
static uint8_t stop_count = 0;
static uint8_t stop_index = 0;
static uint8_t ring_index = 0;
static bool ring_pending = false;
static bool session_started = false;
static time_t ring_until_epoch = 0;
static uint64_t ring_until_ms = 0;
static std::atomic<uint32_t> session_generation{1};
static uint64_t last_schedule_log_ms = 0;
static bool schedule_log_pending = true;
static bool logged_ota_active = false;

static bool session_work_valid(uint32_t value) { return value == session_generation.load(); }

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
        : candidate > now ? "not-due" : now - candidate > 300 ? "outside-grace" : "none";
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
    if (root.size() != 1 || !root["1"].is<JsonObjectConst>()) return fail("Alarm config requires exactly slot 1");
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
    if (!parsed || document["schema"] != 2 || !document["generation"].is<uint64_t>()
        || !document["data"].is<JsonObject>()) return false;
    const uint32_t stored_checksum = document["checksum"].as<uint32_t>();
    if (!document["checksum"].is<uint32_t>()) return false;
    document.remove("checksum");
    std::string payload;
    serializeJson(document, payload);
    return payload.length() <= ALARM_SNAPSHOT_MAX && checksum(payload) == stored_checksum;
}

static bool persist(const AlarmDefinition& candidate, time_t handled, time_t once) {
    if (ota_activity_is_active()) return fail("Firmware update in progress");
    if (generation == UINT64_MAX) return fail("Alarm snapshot generation exhausted");
    AlarmDocument document(ALARM_SNAPSHOT_MAX);
    document["schema"] = 2;
    document["generation"] = generation + 1;
    JsonObject data = document.createNestedObject("data");
    definition_json(candidate, data.createNestedObject("config"));
    data["handled_epoch"] = static_cast<int64_t>(handled);
    data["once_epoch"] = static_cast<int64_t>(once);
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
    storage_error = false;
    last_error[0] = '\0';
    return true;
}

static uint8_t dispatch_hooks(const ButtonAction* actions, uint8_t count, const char* label, uint8_t index) {
#if defined(ARDUINO) && HAS_DISPLAY
    bool locked = false;
    display_manager_lock_if_needed(&locked);
#endif
    for (; index < count; ++index) {
        if (ota_activity_is_active()) break;
        LOGI("Alarm", "%s hook %u/%u: dispatch type=%s", label, unsigned(index + 1), unsigned(count), actions[index].type);
        const ActionResult result = action_dispatch_synchronous(actions[index], label, session_work_valid, session_generation.load());
        if (result != ACTION_COMPLETE) {
            hook_error = true;
            LOGW("Alarm", "%s hook %u/%u: failed result=%u", label, unsigned(index + 1), unsigned(count), unsigned(result));
        } else {
            LOGI("Alarm", "%s hook %u/%u: complete", label, unsigned(index + 1), unsigned(count));
        }
    }
#if defined(ARDUINO) && HAS_DISPLAY
    display_manager_unlock_if_needed(locked);
#endif
    return index;
}

static void stop_session() {
    session_generation.fetch_add(1);
    if (session_started) {
        memcpy(pending_stop, session.on_stop, sizeof(pending_stop));
        stop_count = session.stop_count;
        stop_index = 0;
    }
    session_started = false;
    ring_pending = false;
}

static void flush_hooks() {
    const uint8_t stopped = dispatch_hooks(pending_stop, stop_count, "Alarm stop", stop_index);
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

void alarm_manager_init() {
    std::lock_guard<std::mutex> lock(alarm_mutex);
    definition = AlarmDefinition{};
    session = AlarmDefinition{};
    clock_core = AlarmClockCore{};
    generation = 0;
    once_epoch = 0;
    active_snapshot = -1;
    command_count = 0;
    storage_error = false;
    hook_error = false;
    initialized = false;
    session_generation.fetch_add(1);
    last_error[0] = '\0';
    stop_count = stop_index = ring_index = 0;
    ring_pending = session_started = false;
    last_schedule_log_ms = 0;
    schedule_log_pending = true;
    logged_ota_active = ota_activity_is_active();
    AlarmDocument document(ALARM_SNAPSHOT_MAX);
    DefinitionPtr candidate(static_cast<AlarmDefinition*>(config_psram_alloc(sizeof(AlarmDefinition), "alarm_load")), free);
    if (!candidate) { storage_error = true; fail("Alarm allocation failed"); return; }
    bool invalid_snapshot = false;
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
            || ((!candidate->enabled || candidate->weekdays) && document["data"]["once_epoch"].as<int64_t>() != 0)) { invalid_snapshot = true; continue; }
        const uint64_t stored_generation = document["generation"].as<uint64_t>();
        if (active_snapshot < 0 || stored_generation > generation) {
            definition = *candidate;
            clock_core.handled_epoch = document["data"]["handled_epoch"].as<int64_t>();
            once_epoch = document["data"]["once_epoch"].as<int64_t>();
            generation = stored_generation;
            active_snapshot = index;
        }
    }
    if (invalid_snapshot) {
        storage_error = true;
        fail(active_snapshot < 0 ? "No valid alarm snapshot; alarm disabled" : "Recovered alarm snapshot; storage degraded");
    }
    timezone_revision = time_service_generation();
    clock_core.rearm(time(nullptr), time_service_ready());
    initialized = true;
    LOGI("Alarm", "Initialized: snapshot=%d generation=%llu storage_error=%u", active_snapshot,
        static_cast<unsigned long long>(generation), unsigned(storage_error));
    log_definition("Loaded");
}

bool alarm_config_save_raw(const uint8_t* data, size_t length) {
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
        if (before == after) {
            LOGI("Alarm", "Configuration unchanged; session preserved (%s)", state_name(clock_core.state));
            return true;
        }
        if (stop_index < stop_count) return fail("Alarm cleanup pending");
        const time_t next_once = candidate->enabled && !candidate->weekdays && time_service_ready()
            ? time_service_alarm_next(time(nullptr), candidate->hour, candidate->minute) : 0;
        if (candidate->enabled && !candidate->weekdays && time_service_ready() && !next_once)
            return fail("Cannot schedule one-shot alarm");
        if (!persist(*candidate, clock_core.handled_epoch, next_once)) { storage_error = true; return false; }
        clock_core.cancel();
        stop_session();
        definition = *candidate;
        once_epoch = next_once;
        clock_core.rearm(time(nullptr), time_service_ready());
        schedule_log_pending = true;
        log_definition("Configuration saved");
    }
    flush_hooks();
    return true;
}

const char* alarm_last_error() { return last_error; }

void alarm_config_to_json(JsonObject root) {
    std::lock_guard<std::mutex> lock(alarm_mutex);
    definition_json(definition, root);
}

AlarmSnapshot alarm_snapshot() {
    std::lock_guard<std::mutex> lock(alarm_mutex);
    return {definition.enabled, definition.hour, definition.minute, clock_core.state,
            initialized && time_service_ready(), clock_core.deferred || ring_pending || stop_index < stop_count, storage_error, hook_error.load(), once_epoch};
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
    root["once_epoch"] = static_cast<int64_t>(value.once_epoch);
    char local[32] = {};
    if (value.once_epoch) time_service_format(value.once_epoch, "%Y-%m-%d %H:%M", nullptr, local, sizeof(local));
    root["once_local"] = local;
}

bool alarm_command_submit(const char* command, uint8_t id) {
    if (!initialized || id > 1 || !command) return false;
    const uint8_t operation = !strcmp(command, "cancel") ? 1 : !strcmp(command, "snooze") ? 2 : 0;
    if (!operation) return false;
    std::lock_guard<std::mutex> lock(alarm_mutex);
    if (command_count == sizeof(commands)) return false;
    commands[command_count++] = operation;
    LOGI("Alarm", "Queued command=%s id=%u state=%s", command, unsigned(id), state_name(clock_core.state));
    return true;
}

void alarm_manager_loop() {
    if (!initialized) return;
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
        if (timezone_revision != time_service_generation()) {
            clock_core.cancel();
            stop_session();
            clock_core.rearm(now, time_service_ready());
            timezone_revision = time_service_generation();
            rearmed = true;
            schedule_log_pending = true;
            log_definition("Timezone changed; session dismissed");
        }
        for (uint8_t index = 0; index < command_count; ++index) {
            const bool cancel = commands[index] == 1;
            const bool invalidate = cancel && (clock_core.deferred || ring_pending);
            const bool had_session = clock_core.state != ALARM_IDLE;
            const AlarmState previous_state = clock_core.state;
            const uint8_t command_effects = cancel ? clock_core.cancel() : clock_core.snooze(monotonic);
            if ((cancel && had_session) || (command_effects & ALARM_EFFECT_STOP)) stop_session();
            if (invalidate) clock_core.rearm(now, time_service_ready());
            rearmed = rearmed || (cancel && had_session) || invalidate || command_effects;
            LOGI("Alarm", "Command=%s: %s -> %s deadline_ms=%llu", cancel ? "cancel" : "snooze",
                state_name(previous_state), state_name(clock_core.state), static_cast<unsigned long long>(clock_core.deadline_ms));
        }
        command_count = 0;
        if (ring_pending && (now > ring_until_epoch || monotonic > ring_until_ms)) {
            LOGW("Alarm", "Pending ring expired; discarding remaining ring hooks (%u/%u dispatched)",
                unsigned(ring_index), unsigned(session.ring_count));
            clock_core.cancel();
            stop_session();
        } else if (ring_pending && !ota && !ring_index) {
            clock_core.deadline_ms = monotonic + clock_core.dismiss_ms;
        }
        bool once_recorded = false;
        if (definition.enabled && !definition.weekdays && ready && !ota) {
            if (!once_epoch) {
                const time_t next = time_service_alarm_next(now, definition.hour, definition.minute);
                if (next && persist(definition, clock_core.handled_epoch, next)) {
                    once_epoch = next;
                    schedule_log_pending = true;
                    log_definition("One-shot scheduled");
                } else storage_error = true;
            } else {
                const time_t armed = !clock_core.time_ready ? (now / 60 + 1) * 60 : clock_core.armed_from;
                if (once_epoch < armed || once_epoch <= clock_core.handled_epoch || (once_epoch < now && now - once_epoch > 300)) {
                    definition.enabled = false;
                    if (persist(definition, clock_core.handled_epoch, 0)) {
                        once_epoch = 0;
                        schedule_log_pending = true;
                        log_definition("One-shot expired; disabled");
                    } else {
                        definition.enabled = true;
                        storage_error = true;
                    }
                }
            }
        }
        time_t candidate = definition.enabled && !rearmed && stop_index == stop_count
            ? (definition.weekdays ? time_service_alarm_candidate(now, definition.hour, definition.minute, definition.weekdays) : once_epoch) : 0;
        if (!definition.weekdays && !ota && clock_core.occurrence_due(now, ready, candidate)) {
            definition.enabled = false;
            if (persist(definition, candidate, 0)) {
                once_epoch = 0;
                once_recorded = true;
                schedule_log_pending = true;
                LOGI("Alarm", "One-shot consumed; future scheduling disabled");
            } else {
                definition.enabled = true;
                storage_error = true;
                candidate = 0;
                LOGE("Alarm", "One-shot consumption failed; ring deferred");
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
                ready ? "checking from next full minute" : "waiting for current-boot NTP sync");
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
            if (!once_recorded && !persist(definition, clock_core.handled_epoch, once_epoch)) {
                storage_error = true;
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
            ring_until_epoch = effects & ALARM_EFFECT_HANDLED ? clock_core.handled_epoch + 300 : now + 300;
            ring_until_ms = monotonic + 300000;
        }
    }
    flush_hooks();
}
#endif