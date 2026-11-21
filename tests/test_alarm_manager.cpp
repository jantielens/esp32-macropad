#include <ArduinoJson.h>
#include <map>
#include <string>
#include <cassert>
#include <cstdio>
#include <cstdarg>
#include <vector>
#include <LittleFS.h>
#include "../src/app/alarm_manager.h"
#include "../src/app/action_dispatch.h"
#include "../src/app/action_registry.h"
#include "../src/app/action_parse.h"
#include "../src/app/action_validate.h"

std::map<std::string, std::string> storage_test_files;
bool storage_test_fail_open = false;
size_t storage_test_write_limit = SIZE_MAX;
FakeLittleFS LittleFS;
static bool fail_calloc = false;
static unsigned calloc_calls = 0;
extern "C" void* __real_calloc(size_t count, size_t size);
extern "C" void* __wrap_calloc(size_t count, size_t size) {
    ++calloc_calls;
    return fail_calloc ? nullptr : __real_calloc(count, size);
}
static time_t wall_clock = 1704094140;
static unsigned long monotonic_clock = 0;
static bool ready = false;
static bool ota = false;
static uint32_t timezone_revision = 0;
static const char* timezone_name = "UTC0";
static std::vector<std::string> dispatched;
static std::vector<std::string> dispatched_json;
static ActionResult dispatch_result = ACTION_FAILED;
static size_t ota_after_dispatch = SIZE_MAX;
static bool (*last_work_guard)(uint32_t) = nullptr;
static uint32_t last_work_generation = 0;
static std::vector<std::string> diagnostics;
void log_noop(const char* module, const char* format, ...) {
    if (strcmp(module, "Alarm")) return;
    char message[288];
    va_list arguments;
    va_start(arguments, format);
    const int length = vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    assert(length >= 0 && length < 256);
    diagnostics.emplace_back(message);
}
static bool has_log(const char* text) {
    for (const auto& message : diagnostics) if (message.find(text) != std::string::npos) return true;
    return false;
}
extern "C" time_t time(time_t* output) noexcept { if (output) *output = wall_clock; return wall_clock; }
extern "C" unsigned long millis() { return monotonic_clock; }
bool time_service_ready() { return ready; }
uint32_t time_service_generation() { return timezone_revision; }
void time_service_get_timezone(char* output, size_t capacity) { strlcpy(output, timezone_name, capacity); }
bool time_service_localtime(time_t epoch, struct tm* result) { return result && gmtime_r(&epoch, result); }
bool time_service_format(time_t epoch, const char* format, const char*, char* output, size_t capacity) {
    struct tm local = {};
    return gmtime_r(&epoch, &local) && strftime(output, capacity, format, &local);
}
time_t time_service_alarm_next(time_t now, uint8_t hour, uint8_t minute, uint8_t weekdays) {
    struct tm local = {};
    gmtime_r(&now, &local);
    const time_t target = now - local.tm_hour * 3600 - local.tm_min * 60 - local.tm_sec + hour * 3600 + minute * 60;
    for (int offset = 0; offset < 15; ++offset) {
        const time_t candidate = target + offset * 86400;
        struct tm date = {};
        gmtime_r(&candidate, &date);
        if (candidate > now && (weekdays & (1U << date.tm_wday))) return candidate;
    }
    return 0;
}
time_t time_service_alarm_candidate(time_t now, uint8_t hour, uint8_t minute, uint8_t weekdays) {
    struct tm local = {};
    gmtime_r(&now, &local);
    const time_t target = now - local.tm_hour * 3600 - local.tm_min * 60 - local.tm_sec + hour * 3600 + minute * 60;
    for (int offset = 0; offset >= -8; --offset) {
        const time_t candidate = target + offset * 86400;
        struct tm date = {};
        gmtime_r(&candidate, &date);
        if (candidate <= now && (weekdays & (1U << date.tm_wday))) return candidate;
    }
    return 0;
}
void storage_publish_usage(bool) {}
bool ota_activity_is_active() { return ota; }
ActionDispatchContext action_dispatch_context() { return {}; }
void display_manager_request_binding_refresh() {}
ActionResult action_dispatch(const ButtonAction& action, const char* label, uint32_t, bool) {
    dispatched.emplace_back(std::string(label) + ":" + action.type);
    JsonDocument document;
    action_to_json(action, document.to<JsonObject>());
    std::string json;
    serializeJson(document, json);
    dispatched_json.push_back(json);
    if (dispatched.size() == ota_after_dispatch) ota = true;
    return dispatch_result;
}
ActionResult action_dispatch_synchronous(const ButtonAction& action, const char* label,
                                         bool (*guard)(uint32_t), uint32_t generation, bool) {
    last_work_guard = guard;
    last_work_generation = generation;
    return action_dispatch(action, label, 0);
}

static bool save(const char* json) { return alarm_config_save_raw(reinterpret_cast<const uint8_t*>(json), strlen(json)); }
static const char* config = R"({"action_format_version":1,"lateness_minutes":5,"1":{"enabled":true,"hour":7,"minute":30,"weekdays":62,"snooze_minutes":9,"auto_dismiss_minutes":30,"on_ring":[{"op":"screen.open","args":{"target":"info"}}],"on_stop":[]}})";

static ActionResult test_operation(const ActionResolvedArgument*, const char*, uint32_t) { return ACTION_COMPLETE; }
static const ActionOperationDef test_operations[] = {
    {"test.sync", "Sync", nullptr, 0, test_operation, ACTION_EXECUTION_SYNC},
    {"test.pause", "Pause", nullptr, 0, test_operation, ACTION_EXECUTION_PAUSABLE},
};
static const ActionTypeDef test_owner("test_owner", nullptr, nullptr, ACTION_EXECUTION_PAUSABLE, true, false, test_operations, 2);

static ActionResult dispatch_alarm_operation(const char* json) {
    JsonDocument document;
    assert(!deserializeJson(document, json));
    assert(!action_operation_validate_json(document.as<JsonObjectConst>(), false));
    const ActionOperationDef* operation = action_operation_find(document["op"]);
    assert(operation);
    ActionResolvedArgument arguments[ACTION_OPERATION_MAX_ARGUMENTS] = {};
    for (uint8_t index = 0; index < operation->argument_count; ++index) {
        const auto& argument = operation->arguments[index];
        JsonVariant value = document["args"][argument.name];
        if (argument.type == ACTION_ARGUMENT_ENUM) arguments[index].text = value.as<const char*>();
        else arguments[index].integer = value.as<int32_t>();
    }
    return operation->dispatch(arguments, "alarm test", 0);
}

static void test_alarm_operation_dispatch() {
    const auto previous_dispatched = dispatched;
    const auto previous_dispatched_json = dispatched_json;
    const ActionResult previous_result = dispatch_result;
    const auto previous_guard = last_work_guard;
    const uint32_t previous_generation = last_work_generation;
    storage_test_files.clear();
    ready = true;
    ota = false;
    wall_clock = 1704094140;
    monotonic_clock = 0;
    alarm_manager_init();
    assert(save(config));

    assert(dispatch_alarm_operation(R"({"op":"alarm.set_time","args":{"minutes":1439}})") == ACTION_COMPLETE);
    assert(alarm_snapshot().pending_commands == 1);
    alarm_manager_loop();
    assert(alarm_snapshot().hour == 23 && alarm_snapshot().minute == 59);
    assert(dispatch_alarm_operation(R"({"op":"alarm.adjust_minutes","args":{"minutes":1}})") == ACTION_COMPLETE);
    alarm_manager_loop();
    assert(alarm_snapshot().hour == 0 && alarm_snapshot().minute == 0);
    assert(dispatch_alarm_operation(R"({"op":"alarm.disable","args":{}})") == ACTION_COMPLETE);
    alarm_manager_loop();
    assert(!alarm_snapshot().enabled);
    assert(dispatch_alarm_operation(R"({"op":"alarm.toggle","args":{}})") == ACTION_COMPLETE);
    alarm_manager_loop();
    assert(alarm_snapshot().enabled);
    assert(dispatch_alarm_operation(R"({"op":"alarm.enable","args":{}})") == ACTION_COMPLETE);
    alarm_manager_loop();
    assert(alarm_snapshot().enabled);

    assert(dispatch_alarm_operation(R"({"op":"alarm.weekday_enable","args":{"day":"0"}})") == ACTION_COMPLETE);
    alarm_manager_loop();
    assert(alarm_snapshot().weekdays == 63);
    assert(dispatch_alarm_operation(R"({"op":"alarm.weekday_toggle","args":{"day":"6"}})") == ACTION_COMPLETE);
    alarm_manager_loop();
    assert(alarm_snapshot().weekdays == 127);
    assert(dispatch_alarm_operation(R"({"op":"alarm.weekday_disable","args":{"day":"0"}})") == ACTION_COMPLETE);
    alarm_manager_loop();
    assert(alarm_snapshot().weekdays == 126);

    JsonDocument invalid;
    assert(!deserializeJson(invalid, R"({"op":"alarm.set_time","args":{"minutes":1440}})"));
    assert(action_operation_validate_json(invalid.as<JsonObjectConst>(), false));
    assert(!deserializeJson(invalid, R"({"op":"alarm.weekday_toggle","args":{"day":"7"}})"));
    assert(action_operation_validate_json(invalid.as<JsonObjectConst>(), false));
    assert(!deserializeJson(invalid, R"({"op":"alarm.cancel","args":{"alarm":"2"}})"));
    assert(action_operation_validate_json(invalid.as<JsonObjectConst>(), false));
    assert(alarm_snapshot().pending_commands == 0);
    const ActionOperationDef* weekday = action_operation_find("alarm.weekday_toggle");
    const char* text = nullptr;
    assert(!action_argument_normalize_text(weekday->arguments[0], "6", &text, nullptr));
    assert(!strcmp(text, "6"));
    assert(action_argument_normalize_text(weekday->arguments[0], "7", &text, nullptr));
    const ActionOperationDef* adjustment = action_operation_find("alarm.adjust_minutes");
    int32_t minutes = 0;
    assert(!action_argument_normalize_text(adjustment->arguments[0], "-10", nullptr, &minutes));
    assert(minutes == -10);
    assert(action_argument_normalize_text(adjustment->arguments[0], "1.5", nullptr, &minutes));

    for (unsigned index = 0; index < 4; ++index)
        assert(dispatch_alarm_operation(R"({"op":"alarm.snooze","args":{"alarm":"0"}})") == ACTION_COMPLETE);
    assert(dispatch_alarm_operation(R"({"op":"alarm.cancel","args":{"alarm":"1"}})") == ACTION_FAILED);
    assert(alarm_snapshot().pending_commands == 4 && alarm_snapshot().command_error);
    for (unsigned index = 0; index < 4; ++index) alarm_manager_loop();
    assert(dispatch_alarm_operation(R"({"op":"alarm.cancel","args":{"alarm":"1"}})") == ACTION_COMPLETE);
    alarm_manager_loop();

    storage_test_files.clear();
    wall_clock = 1704094140;
    alarm_manager_init();
    dispatch_result = ACTION_COMPLETE;
    assert(save(config));
    wall_clock += 60;
    alarm_manager_loop();
    assert(alarm_snapshot().state == ALARM_RINGING);
    assert(dispatch_alarm_operation(R"({"op":"alarm.snooze","args":{"alarm":"0"}})") == ACTION_COMPLETE);
    alarm_manager_loop();
    assert(alarm_snapshot().state == ALARM_SNOOZED);
    assert(dispatch_alarm_operation(R"({"op":"alarm.cancel","args":{"alarm":"1"}})") == ACTION_COMPLETE);
    alarm_manager_loop();
    assert(alarm_snapshot().state == ALARM_IDLE);
    dispatched = previous_dispatched;
    dispatched_json = previous_dispatched_json;
    dispatch_result = previous_result;
    last_work_guard = previous_guard;
    last_work_generation = previous_generation;
}

static void test_typed_hooks() {
    action_type_register(&test_owner);
    storage_test_files.clear();
    ready = true;
    ota = false;
    wall_clock = 1704094140;
    monotonic_clock = 0;
    alarm_manager_init();
    const char* typed = R"({"action_format_version":1,"lateness_minutes":5,"1":{"enabled":true,"hour":7,"minute":30,"weekdays":62,"snooze_minutes":9,"auto_dismiss_minutes":30,"on_ring":[{"op":"test.sync","args":{}},{"op":"screen.open","args":{"target":"info"}}],"on_stop":[{"op":"screen.open","args":{"target":"settings"}}]}})";
    assert(save(typed));
    const auto persisted = storage_test_files;
    std::string invalid(typed);
    invalid.replace(invalid.find("test.sync"), strlen("test.sync"), "test.pause");
    assert(!save(invalid.c_str()) && storage_test_files == persisted);
    invalid = typed;
    invalid.replace(invalid.find("\"target\":\"info\""), strlen("\"target\":\"info\""), "\"target\":42");
    assert(!save(invalid.c_str()) && storage_test_files == persisted);
    invalid = typed;
    invalid.insert(invalid.size() - 1, R"(,"1\u0000x":{"on_ring":[{"type":"legacy"}]})");
    assert(!save(invalid.c_str()) && storage_test_files == persisted);
    std::string replacement(typed);
    replacement.replace(replacement.find("\"settings\""), strlen("\"settings\""), "\"new\"");
    storage_test_write_limit = 8;
    assert(!save(replacement.c_str()));
    storage_test_write_limit = SIZE_MAX;
    alarm_manager_init();
    JsonDocument saved;
    alarm_config_to_json(saved.to<JsonObject>());
    assert(saved["1"]["on_ring"][0]["op"] == "test.sync");
    assert(saved["1"]["on_stop"][0]["args"]["target"] == "settings");
    dispatched.clear();
    dispatched_json.clear();
    dispatch_result = ACTION_COMPLETE;
    ota_after_dispatch = 1;
    wall_clock += 60;
    alarm_manager_loop();
    assert(ota && dispatched_json.size() == 1);
    ota = false;
    ota_after_dispatch = SIZE_MAX;
    assert(save(replacement.c_str()));
    assert(dispatched_json.size() == 2);
    assert(dispatched_json.back().find("\"target\":\"settings\"") != std::string::npos);
    assert(dispatched_json.back().find("\"target\":\"new\"") == std::string::npos);
    alarm_config_to_json(saved.to<JsonObject>());
    assert(saved["1"]["on_stop"][0]["args"]["target"] == "new");

    storage_test_files.clear();
    wall_clock = 1704094140;
    alarm_manager_init();
    assert(save(typed));
    dispatched.clear();
    dispatched_json.clear();
    dispatch_result = ACTION_FAILED;
    wall_clock += 60;
    alarm_manager_loop();
    assert(dispatched_json.size() == 2 && alarm_snapshot().hook_error);
}

static void test_scoped_saves() {
    storage_test_files.clear();
    ready = true;
    ota = false;
    wall_clock = 1704094140;
    monotonic_clock = 0;
    alarm_manager_init();
    assert(save(config));
    const auto save_section = [](const char* json, AlarmConfigSection section) {
        return alarm_config_save_section_raw(reinterpret_cast<const uint8_t*>(json), strlen(json), section);
    };
    assert(save_section(R"({"enabled":false,"hour":8,"minute":15,"weekdays":31})", AlarmConfigSection::Schedule));
    JsonDocument current;
    alarm_config_to_json(current.to<JsonObject>());
    assert(current["lateness_minutes"] == 5);
    assert(current["1"]["snooze_minutes"] == 9);
    assert(current["1"]["on_ring"].size() == 1);
    assert(save_section(R"({"action_format_version":1,"lateness_minutes":12,"snooze_minutes":3,"auto_dismiss_minutes":20,"on_ring":[],"on_stop":[]})", AlarmConfigSection::Behavior));
    alarm_config_to_json(current.to<JsonObject>());
    assert(current["1"]["enabled"] == false);
    assert(current["1"]["hour"] == 8 && current["1"]["minute"] == 15);
    assert(current["1"]["weekdays"] == 31);
    assert(current["lateness_minutes"] == 12 && current["1"]["snooze_minutes"] == 3);
    assert(!save_section(R"({"enabled":true,"hour":9,"minute":0,"snooze_minutes":1})", AlarmConfigSection::Schedule));
    const auto persisted = storage_test_files;
    assert(!save_section(R"({"enabled":false,"hour":8,"minute":15,"weekdays":31,"hour\u0000x":9})", AlarmConfigSection::Schedule));
    assert(storage_test_files == persisted);
    alarm_manager_init();
    alarm_config_to_json(current.to<JsonObject>());
    assert(current["1"]["hour"] == 8 && current["1"]["minute"] == 15);
    assert(current["lateness_minutes"] == 12 && current["1"]["auto_dismiss_minutes"] == 20);
}

static void test_once() {
    std::string once(config);
    once.replace(once.find("\"weekdays\":62"), strlen("\"weekdays\":62"), "\"weekdays\":0");
    storage_test_files.clear();
    ready = true;
    wall_clock = 1704094140;
    monotonic_clock = 0;
    alarm_manager_init();
    assert(save(once.c_str()));
    const time_t target = wall_clock + 60;
    assert(alarm_snapshot().once_epoch == target);
    wall_clock += 30;
    alarm_manager_init();
    assert(alarm_snapshot().once_epoch == target);
    assert(save(once.c_str()));
    assert(alarm_snapshot().once_epoch == target);
    ++timezone_revision;
    alarm_manager_loop();
    assert(alarm_snapshot().once_epoch == target);
    JsonDocument status;
    alarm_status_to_json(status.to<JsonObject>());
    assert(status["enabled"] == true && status["once_epoch"].as<int64_t>() == target);
    assert(!strcmp(status["once_local"].as<const char*>(), "2024-01-01 07:30"));
    wall_clock += 30;
    dispatched.clear();
    alarm_manager_loop();
    assert(alarm_snapshot().state == ALARM_RINGING && !alarm_snapshot().enabled);
    assert(alarm_snapshot().once_epoch == 0 && dispatched.size() == 1);
    assert(last_work_guard(last_work_generation));
    assert(alarm_command_submit("snooze"));
    alarm_manager_loop();
    assert(alarm_snapshot().state == ALARM_SNOOZED && !last_work_guard(last_work_generation));
    monotonic_clock += 9 * 60000;
    wall_clock += 9 * 60;
    alarm_manager_loop();
    assert(alarm_snapshot().state == ALARM_RINGING && dispatched.size() == 2);
    monotonic_clock += 30 * 60000;
    wall_clock += 30 * 60;
    alarm_manager_loop();
    assert(alarm_snapshot().state == ALARM_IDLE);
    alarm_manager_init();
    wall_clock += 86400;
    alarm_manager_loop();
    assert(!alarm_snapshot().enabled && dispatched.size() == 2);
    assert(save(once.c_str()));
    assert(alarm_snapshot().enabled && alarm_snapshot().once_epoch > wall_clock);
    storage_test_files.clear();
    wall_clock = 1704094140;
    alarm_manager_init();
    assert(save(once.c_str()));
    storage_test_write_limit = 8;
    wall_clock += 60;
    dispatched.clear();
    alarm_manager_loop();
    assert(!alarm_snapshot().enabled && alarm_snapshot().storage_error);
    assert(alarm_snapshot().state == ALARM_RINGING && dispatched.size() == 1);
    storage_test_write_limit = SIZE_MAX;
    ++wall_clock;
    alarm_manager_loop();
    assert(!alarm_snapshot().enabled && alarm_snapshot().state == ALARM_RINGING && dispatched.size() == 1);
    storage_test_files.clear();
    wall_clock = 1704094140;
    alarm_manager_init();
    assert(save(once.c_str()));
    wall_clock += 361;
    dispatched.clear();
    alarm_manager_loop();
    assert(!alarm_snapshot().enabled && alarm_snapshot().state == ALARM_IDLE && dispatched.empty());
    alarm_manager_init();
    assert(!alarm_snapshot().enabled);
    JsonDocument invalid_adjustment;
    assert(!deserializeJson(invalid_adjustment, R"({"op":"alarm.adjust_minutes","args":{"minutes":"1.5"}})"));
    assert(action_operation_validate_json(invalid_adjustment.as<JsonObjectConst>(), false));
    assert(alarm_snapshot().pending_commands == 0);
    storage_test_files.clear();
    wall_clock = 1704094140;
    alarm_manager_init();
    assert(save(once.c_str()));
    wall_clock += 61;
    alarm_manager_init();
    dispatched.clear();
    alarm_manager_loop();
    assert(!alarm_snapshot().enabled && dispatched.size() == 1);
    storage_test_files.clear();
    ready = false;
    wall_clock = 1704094140;
    alarm_manager_init();
    assert(save(once.c_str()));
    assert(alarm_snapshot().once_epoch == 0);
    wall_clock += 120;
    ready = true;
    alarm_manager_loop();
    assert(alarm_snapshot().enabled && alarm_snapshot().once_epoch == 1704180600);
    storage_test_files.clear();
    ready = true;
    wall_clock = 1704094140;
    alarm_manager_init();
    assert(save(once.c_str()));
    ota = true;
    wall_clock += 60;
    dispatched.clear();
    alarm_manager_loop();
    assert(alarm_snapshot().deferred && dispatched.empty());
    ota = false;
    wall_clock += 300;
    alarm_manager_loop();
    assert(!alarm_snapshot().enabled && alarm_snapshot().state == ALARM_RINGING && dispatched.size() == 1);
    storage_test_files.clear();
    ready = false;
    ota = false;
    wall_clock = 1704094140;
    monotonic_clock = 0;
    dispatched.clear();
    diagnostics.clear();
}

static uint64_t persisted_generation() {
    uint64_t newest = 0;
    for (const auto& file : storage_test_files) {
        JsonDocument document;
        if (!deserializeJson(document, file.second))
            newest = std::max(newest, document["generation"].as<uint64_t>());
    }
    return newest;
}

static void test_recovery() {
    const time_t start = 1704094140;
    std::string recovery_config(config);
    recovery_config.replace(recovery_config.find("\"lateness_minutes\":5"), strlen("\"lateness_minutes\":5"), "\"lateness_minutes\":360");
    auto prepare = [&] {
        storage_test_files.clear();
        storage_test_fail_open = false;
        storage_test_write_limit = SIZE_MAX;
        ready = true;
        ota = false;
        timezone_name = "UTC0";
        wall_clock = start;
        monotonic_clock = 0;
        dispatched.clear();
        alarm_manager_init();
        assert(alarm_snapshot().lateness_minutes == 360);
        assert(save(recovery_config.c_str()));
    };
    auto reboot = [&](time_t elapsed, bool synchronized) {
        wall_clock = start + elapsed;
        monotonic_clock = 0;
        ready = synchronized;
        alarm_manager_init();
        alarm_manager_loop();
    };
    prepare();
    std::string legacy(recovery_config);
    legacy.erase(1, legacy.find("\"1\"") - 1);
    assert(!save(legacy.c_str()));
    std::string invalid(recovery_config);
    invalid.replace(invalid.find("360"), 3, "10081");
    assert(!save(invalid.c_str()));
    invalid = recovery_config;
    invalid.replace(invalid.find("360"), 3, "1.5");
    assert(!save(invalid.c_str()));
    reboot(60 + 21600, false);
    assert(dispatched.empty());
    ready = true;
    alarm_manager_loop();
    assert(alarm_snapshot().state == ALARM_RINGING && dispatched.size() == 1);
    assert(alarm_command_submit("cancel"));
    alarm_manager_loop();
    reboot(60 + 21600, true);
    assert(alarm_snapshot().state == ALARM_IDLE && dispatched.size() == 1);
    wall_clock = start;
    alarm_manager_loop();
    wall_clock += 600;
    alarm_manager_loop();
    assert(dispatched.size() == 1);

    prepare();
    reboot(60 + 21601, true);
    assert(alarm_snapshot().state == ALARM_IDLE && dispatched.empty());
    prepare();
    reboot(600, false);
    wall_clock = start + 60 + 21601;
    ready = true;
    alarm_manager_loop();
    assert(dispatched.empty());
    prepare();
    reboot(21 * 86400 + 600, true);
    assert(dispatched.size() == 1);
    alarm_manager_loop();
    assert(dispatched.size() == 1);

    prepare();
    wall_clock += 600;
    assert(alarm_command_submit("disable", 1));
    alarm_manager_loop();
    assert(alarm_command_submit("enable", 1));
    alarm_manager_loop();
    JsonDocument edited;
    alarm_config_to_json(edited.to<JsonObject>());
    std::string payload;
    serializeJson(edited, payload);
    assert(save(payload.c_str()));
    reboot(601, true);
    assert(dispatched.empty());

    prepare();
    ready = false;
    assert(alarm_command_submit("adjust_minutes", 1, 1));
    alarm_manager_loop();
    alarm_config_to_json(edited.to<JsonObject>());
    payload.clear();
    serializeJson(edited, payload);
    assert(save(payload.c_str()));
    reboot(600, false);
    ready = true;
    alarm_manager_loop();
    reboot(601, true);
    assert(dispatched.empty());

    prepare();
    timezone_name = "CET-1";
    reboot(600, true);
    assert(dispatched.empty());
    reboot(601, true);
    assert(dispatched.empty());

    prepare();
    reboot(600, false);
    storage_test_fail_open = true;
    ready = true;
    alarm_manager_loop();
    assert(dispatched.size() == 1 && alarm_snapshot().storage_error && alarm_snapshot().save_pending);
    alarm_manager_loop();
    assert(dispatched.size() == 1);
    storage_test_fail_open = false;
    monotonic_clock += 10000;
    alarm_manager_loop();
    assert(!alarm_snapshot().storage_error && !alarm_snapshot().save_pending);
    reboot(601, true);
    assert(dispatched.size() == 1);

    prepare();
    reboot(600, false);
    storage_test_fail_open = true;
    ready = true;
    alarm_manager_loop();
    assert(dispatched.size() == 1);
    storage_test_fail_open = false;
    reboot(601, true);
    assert(dispatched.size() == 2);

    prepare();
    std::string on_time(recovery_config);
    on_time.replace(on_time.find("360"), 3, "0");
    assert(save(on_time.c_str()));
    reboot(61, true);
    assert(dispatched.empty());
    prepare();
    assert(save(on_time.c_str()));
    reboot(60, true);
    assert(dispatched.size() == 1);

    prepare();
    wall_clock += 600;
    std::string wider(recovery_config);
    wider.replace(wider.find("360"), 3, "720");
    assert(save(wider.c_str()));
    alarm_manager_loop();
    assert(dispatched.size() == 1 && alarm_snapshot().lateness_minutes == 720);
    assert(save(recovery_config.c_str()));
    assert(alarm_snapshot().state == ALARM_RINGING);

    prepare();
    std::string once(recovery_config);
    once.replace(once.find("\"weekdays\":62"), strlen("\"weekdays\":62"), "\"weekdays\":0");
    assert(save(once.c_str()));
    reboot(60 + 21600, false);
    ready = true;
    storage_test_fail_open = true;
    alarm_manager_loop();
    assert(dispatched.size() == 1 && !alarm_snapshot().enabled && alarm_snapshot().storage_error);
    assert(alarm_snapshot().once_epoch == 0 && alarm_snapshot().state == ALARM_RINGING);
    storage_test_fail_open = false;
    monotonic_clock += 10000;
    alarm_manager_loop();
    assert(!alarm_snapshot().storage_error);
    reboot(60 + 21600, true);
    assert(dispatched.size() == 1 && !alarm_snapshot().enabled);
    prepare();
    assert(save(once.c_str()));
    reboot(60 + 21601, true);
    assert(dispatched.empty() && !alarm_snapshot().enabled);

    prepare();
    ready = false;
    assert(save(once.c_str()));
    ready = true;
    storage_test_fail_open = true;
    alarm_manager_loop();
    assert(alarm_snapshot().once_epoch == start + 60);
    wall_clock = start + 60;
    alarm_manager_loop();
    assert(dispatched.size() == 1 && !alarm_snapshot().enabled && alarm_snapshot().storage_error);
    storage_test_fail_open = false;
    monotonic_clock += 10000;
    alarm_manager_loop();
    assert(!alarm_snapshot().storage_error);

    prepare();
    std::string long_window(recovery_config);
    long_window.replace(long_window.find("360"), 3, "10080");
    long_window.replace(long_window.find("\"weekdays\":62"), strlen("\"weekdays\":62"), "\"weekdays\":2");
    assert(save(long_window.c_str()));
    reboot(2 * 86400 + 600, true);
    assert(dispatched.size() == 1);

    storage_test_files.clear();
    dispatched.clear();
    diagnostics.clear();
    ready = false;
    wall_clock = start;
    monotonic_clock = 0;
}

static void test_settings_debounce() {
    storage_test_files.clear();
    ready = true;
    ota = false;
    wall_clock = 1704094140;
    monotonic_clock = 0;
    alarm_manager_init();
    assert(save(config));
    const auto initial_files = storage_test_files;
    const uint64_t initial_generation = persisted_generation();
    assert(alarm_command_submit("adjust_minutes", 1, 1));
    alarm_manager_loop();
    assert(alarm_snapshot().minute == 31 && alarm_snapshot().save_pending);
    assert(alarm_snapshot().pending_commands == 0 && storage_test_files == initial_files);
    JsonDocument status;
    alarm_status_to_json(status.to<JsonObject>());
    assert(status["save_state"] == "pending" && status["completed_commands"] == 1);
    monotonic_clock = 5000;
    assert(alarm_command_submit("adjust_minutes", 1, 1));
    alarm_manager_loop();
    assert(alarm_snapshot().minute == 32 && storage_test_files == initial_files);
    monotonic_clock = 10000;
    assert(alarm_command_submit("adjust_minutes", 1, 0));
    alarm_manager_loop();
    assert(storage_test_files == initial_files);
    monotonic_clock = 14999;
    alarm_manager_loop();
    assert(storage_test_files == initial_files);
    monotonic_clock = 15000;
    alarm_manager_loop();
    assert(persisted_generation() == initial_generation + 1 && !alarm_snapshot().save_pending);
    alarm_status_to_json(status.to<JsonObject>());
    assert(status["save_state"] == "saved");
    assert(alarm_command_submit("adjust_minutes", 1, 1));
    alarm_manager_loop();
    assert(alarm_snapshot().minute == 33);
    alarm_manager_init();
    assert(alarm_snapshot().minute == 32 && !alarm_snapshot().save_pending);
    assert(alarm_command_submit("adjust_minutes", 1, 1));
    alarm_manager_loop();
    JsonDocument live;
    alarm_config_to_json(live.to<JsonObject>());
    std::string payload;
    serializeJson(live, payload);
    const uint64_t before_explicit = persisted_generation();
    assert(save(payload.c_str()));
    assert(persisted_generation() == before_explicit + 1 && !alarm_snapshot().save_pending);
    alarm_manager_init();
    assert(alarm_snapshot().minute == 33);
    assert(alarm_command_submit("adjust_minutes", 1, 1));
    alarm_manager_loop();
    assert(alarm_snapshot().minute == 34);
    storage_test_write_limit = 8;
    monotonic_clock += 10000;
    alarm_manager_loop();
    assert(alarm_snapshot().minute == 34 && alarm_snapshot().save_pending && alarm_snapshot().save_failed);
    alarm_status_to_json(status.to<JsonObject>());
    assert(status["save_state"] == "failed" && status["command_error"] == true);
    assert(strlen(status["command_message"].as<const char*>()) > 0);
    const auto failed_files = storage_test_files;
    storage_test_write_limit = SIZE_MAX;
    monotonic_clock += 9999;
    alarm_manager_loop();
    assert(storage_test_files == failed_files);
    ++monotonic_clock;
    alarm_manager_loop();
    assert(!alarm_snapshot().save_pending && !alarm_snapshot().command_error);
    alarm_status_to_json(status.to<JsonObject>());
    assert(status["save_state"] == "saved");
    alarm_manager_init();
    assert(alarm_snapshot().minute == 34);
    assert(alarm_command_submit("weekday_toggle", 1, 0, 0));
    alarm_manager_loop();
    const auto before_ota = storage_test_files;
    ota = true;
    monotonic_clock += 10000;
    alarm_manager_loop();
    assert(storage_test_files == before_ota && alarm_snapshot().save_pending);
    ota = false;
    alarm_manager_loop();
    assert(!alarm_snapshot().save_pending && storage_test_files != before_ota);
    alarm_manager_init();
    assert(alarm_snapshot().weekdays == 63);
    for (bool once : {false, true}) {
        storage_test_files.clear();
        monotonic_clock = 0;
        wall_clock = 1704094195;
        alarm_manager_init();
        std::string schedule(config);
        if (once) schedule.replace(schedule.find("\"weekdays\":62"), strlen("\"weekdays\":62"), "\"weekdays\":0");
        assert(save(schedule.c_str()));
        const uint64_t before_ring = persisted_generation();
        assert(alarm_command_submit("set_time", 1, 451));
        alarm_manager_loop();
        assert(alarm_command_submit("set_time", 1, 450));
        alarm_manager_loop();
        assert(alarm_snapshot().save_pending && persisted_generation() == before_ring);
        wall_clock += 5;
        monotonic_clock = 5000;
        dispatched.clear();
        alarm_manager_loop();
        assert(alarm_snapshot().state == ALARM_RINGING && dispatched.size() == 1);
        assert(persisted_generation() == before_ring + 1 && !alarm_snapshot().save_pending);
        if (once) assert(!alarm_snapshot().enabled);
        const auto ring_files = storage_test_files;
        monotonic_clock = 15000;
        alarm_manager_loop();
        assert(storage_test_files == ring_files && alarm_snapshot().state == ALARM_RINGING);
        storage_test_write_limit = 8;
        assert(alarm_command_submit("adjust_minutes", 1, 1));
        alarm_manager_loop();
        assert(alarm_snapshot().state == ALARM_IDLE && alarm_snapshot().minute == 31);
        assert(alarm_snapshot().save_pending && !alarm_snapshot().command_error);
        monotonic_clock += 10000;
        alarm_manager_loop();
        assert(alarm_snapshot().state == ALARM_IDLE && alarm_snapshot().minute == 31 && alarm_snapshot().save_failed);
        storage_test_write_limit = SIZE_MAX;
    }
    storage_test_files.clear();
    ready = false;
    ota = false;
    wall_clock = 1704094140;
    monotonic_clock = 0;
    dispatched.clear();
    diagnostics.clear();
}

static void test_state_allocation_failure() {
    fail_calloc = true;
    alarm_manager_init();
    const AlarmSnapshot unavailable = alarm_snapshot();
    assert(!unavailable.ready && !unavailable.enabled && unavailable.storage_error);
    JsonDocument document;
    assert(!alarm_config_to_json(document.to<JsonObject>()));
    assert(!alarm_command_submit("cancel"));
    assert(!save(config));
    alarm_manager_loop();
    fail_calloc = false;
    ready = true;
    alarm_manager_init();
    assert(alarm_snapshot().ready && !alarm_snapshot().storage_error);
    const unsigned allocated = calloc_calls;
    alarm_manager_init();
    assert(calloc_calls == allocated + 1);
    assert(alarm_snapshot().ready && !alarm_snapshot().storage_error);
    assert(save(config));
}

static void test_incompatible_hook_snapshot_can_be_replaced() {
    action_type_register(&test_owner);
    const char* invalid_actions[] = {
        R"({"op":"missing.operation","args":{}})",
        R"({"type":"screen","op":"test.sync","args":{}})",
        R"({"type":"screen","op":"test.sync"})",
        R"({"op":"test.sync","args":{"extra":true}})",
        R"({"type":"unknown","data":"preserve"})",
        nullptr
    };
    for (const char* invalid : invalid_actions) {
        for (bool older_snapshot : {false, true}) {
            storage_test_files.clear();
            ready = true;
            ota = false;
            timezone_name = "UTC0";
            alarm_manager_init();
            if (older_snapshot) assert(save(config));
            std::string changed(config);
            changed.replace(changed.find("30,"), 3, "31,");
            assert(save(changed.c_str()));

            const char* path = older_snapshot ? "/config/alarm_b.json" : "/config/alarm_a.json";
            JsonDocument stored;
            assert(!deserializeJson(stored, storage_test_files[path]));
            if (invalid) {
                JsonDocument action;
                assert(!deserializeJson(action, invalid));
                stored["data"]["config"]["1"]["on_ring"][0].set(action);
            } else {
                stored["data"]["config"].as<JsonObject>().remove("action_format_version");
            }
            stored.remove("checksum");
            std::string payload;
            serializeJson(stored, payload);
            uint32_t crc = 0xffffffffU;
            for (unsigned char byte : payload) {
                crc ^= byte;
                for (int bit = 0; bit < 8; ++bit)
                    crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
            }
            stored["checksum"] = ~crc;
            storage_test_files[path].clear();
            serializeJson(stored, storage_test_files[path]);
            const auto before = storage_test_files;
            timezone_name = "changed";
            alarm_manager_init();
            assert(alarm_snapshot().ready && !alarm_snapshot().enabled);
            JsonDocument document;
            assert(alarm_config_to_json(document.to<JsonObject>()));
            assert(document["config_reset"] == true);
            assert(document["1"]["on_ring"].size() == 0);
            assert(document["1"]["on_stop"].size() == 0);
            alarm_manager_loop();
            assert(storage_test_files == before);
            std::string replacement;
            serializeJson(document, replacement);
            storage_test_fail_open = true;
            assert(!save(replacement.c_str()));
            storage_test_fail_open = false;
            assert(storage_test_files == before);
            assert(alarm_config_to_json(document.to<JsonObject>()));
            assert(document["config_reset"] == true);
            if (!invalid && older_snapshot) {
                storage_test_write_limit = 8;
                assert(!save(replacement.c_str()));
                storage_test_write_limit = SIZE_MAX;
                assert(storage_test_files[path] == before.at(path));
                alarm_manager_init();
                assert(alarm_config_to_json(document.to<JsonObject>()));
                assert(document["config_reset"] == true && !alarm_snapshot().enabled);
            }
            if (!invalid) {
                const char* section = older_snapshot
                    ? R"({"action_format_version":1,"lateness_minutes":360,"snooze_minutes":9,"auto_dismiss_minutes":30,"on_ring":[],"on_stop":[]})"
                    : R"({"enabled":false,"hour":7,"minute":0,"weekdays":0})";
                assert(alarm_config_save_section_raw(reinterpret_cast<const uint8_t*>(section), strlen(section),
                    older_snapshot ? AlarmConfigSection::Behavior : AlarmConfigSection::Schedule));
            } else {
                assert(save(replacement.c_str()));
            }
            assert(storage_test_files[path] == before.at(path));
            alarm_manager_init();
            assert(alarm_config_to_json(document.to<JsonObject>()));
            assert(document["config_reset"] == false);
            assert(alarm_snapshot().ready && !alarm_snapshot().enabled);
            assert(save(config));
            alarm_manager_init();
            assert(alarm_snapshot().ready && alarm_snapshot().enabled);
        }
    }
    storage_test_files.clear();
    timezone_name = "UTC0";
    alarm_manager_init();
    assert(alarm_snapshot().ready);
    std::string legacy(config);
    legacy.replace(legacy.find("{\"op\":\"screen.open\",\"args\":{\"target\":\"info\"}}"),
        strlen("{\"op\":\"screen.open\",\"args\":{\"target\":\"info\"}}"),
        "{\"type\":\"visual_alert\",\"op\":\"start\",\"color\":\"#ff0000\"}");
    const auto before = storage_test_files;
    assert(!save(legacy.c_str()));
    assert(storage_test_files == before);
}

int main(int argc, char** argv) {
    if (argc > 1 && !strcmp(argv[1], "allocation-failure")) {
        test_state_allocation_failure();
        std::puts("alarm manager allocation-failure: PASS");
        return 0;
    }
    test_incompatible_hook_snapshot_can_be_replaced();
    test_scoped_saves();
    test_typed_hooks();
    test_alarm_operation_dispatch();
    test_recovery();
    test_settings_debounce();
    test_once();
    alarm_manager_init();
    assert(!alarm_snapshot().enabled);
    assert(!alarm_command_submit("adjust_minutes", 0, 1));
    assert(!alarm_command_submit("set_time", 1, 1440));
    assert(!alarm_command_submit("weekday_toggle", 1, 0, 7));
    assert(!alarm_command_submit("unknown", 1));
    assert(alarm_command_submit("set_time", 1, 1439));
    alarm_manager_loop();
    assert(alarm_snapshot().hour == 23 && alarm_snapshot().minute == 59);
    assert(alarm_command_submit("adjust_minutes", 1, 1));
    alarm_manager_loop();
    assert(alarm_snapshot().hour == 0 && alarm_snapshot().minute == 0);
    assert(alarm_command_submit("adjust_minutes", 1, -1));
    alarm_manager_loop();
    assert(alarm_snapshot().hour == 23 && alarm_snapshot().minute == 59);
    storage_test_write_limit = 8;
    assert(alarm_command_submit("set_time", 1, 60));
    alarm_manager_loop();
    assert(alarm_snapshot().hour == 1 && alarm_snapshot().minute == 0);
    assert(alarm_snapshot().save_pending && !alarm_snapshot().command_error);
    monotonic_clock += 10000;
    alarm_manager_loop();
    storage_test_write_limit = SIZE_MAX;
    assert(alarm_snapshot().command_error && alarm_snapshot().completed_commands == 4);
    assert(alarm_command_submit("weekday_enable", 1, 0, 1));
    assert(alarm_command_submit("weekday_toggle", 1, 0, 6));
    assert(alarm_command_submit("weekday_disable", 1, 0, 1));
    assert(alarm_command_submit("enable", 1));
    assert(!alarm_command_submit("toggle", 1));
    assert(alarm_snapshot().pending_commands == 4);
    alarm_manager_loop();
    assert(alarm_snapshot().weekdays == 2 && alarm_snapshot().pending_commands == 3);
    alarm_manager_loop();
    assert(alarm_snapshot().weekdays == 66);
    alarm_manager_loop();
    assert(alarm_snapshot().weekdays == 64);
    alarm_manager_loop();
    assert(alarm_snapshot().enabled);
    monotonic_clock += 10000;
    alarm_manager_loop();
    assert(!alarm_snapshot().command_error && !alarm_snapshot().save_pending);
    const auto saved_files = storage_test_files;
    assert(alarm_command_submit("enable", 1));
    alarm_manager_loop();
    assert(saved_files == storage_test_files);
    for (int tick = 0; tick < 100; ++tick) alarm_manager_loop();
    assert(saved_files == storage_test_files);
    alarm_manager_init();
    assert(alarm_snapshot().enabled && alarm_snapshot().weekdays == 64);
    assert(alarm_command_submit("disable", 1));
    alarm_manager_loop();
    assert(!alarm_snapshot().enabled && alarm_snapshot().weekdays == 64);
    assert(alarm_command_submit("toggle", 1));
    alarm_manager_loop();
    assert(alarm_snapshot().enabled);
    assert(alarm_command_submit("adjust_minutes", 1, INT32_MAX));
    alarm_manager_loop();
    assert(alarm_snapshot().hour * 60 + alarm_snapshot().minute == (60LL + INT32_MAX) % 1440);
    storage_test_files.clear();
    alarm_manager_init();
    assert(save(config));
    assert(alarm_snapshot().enabled && alarm_snapshot().minute == 30);
    alarm_manager_init();
    assert(alarm_snapshot().enabled && alarm_snapshot().minute == 30);
    storage_test_write_limit = 8;
    std::string changed(config);
    changed.replace(changed.find("30,"), 3, "31,");
    assert(!save(changed.c_str()));
    assert(alarm_snapshot().minute == 30);
    alarm_manager_init();
    assert(alarm_snapshot().enabled && alarm_snapshot().minute == 30);
    storage_test_write_limit = SIZE_MAX;
    assert(save(changed.c_str()));
    alarm_manager_init();
    assert(alarm_snapshot().minute == 31);
    assert(!save(R"({"action_format_version":1,"lateness_minutes":5,"1":{"enabled":true,"hour":7,"minute":30,"weekdays":62,"snooze_minutes":9,"auto_dismiss_minutes":30,"on_ring":[{"op":"test.pause","args":{}}],"on_stop":[]}})"));
    assert(!save(R"({"action_format_version":1,"lateness_minutes":5,"1":{"enabled":true,"hour":7,"minute":30,"weekdays":62,"snooze_minutes":9,"auto_dismiss_minutes":30,"on_ring":[{"type":"key","key_sequence":"A"}],"on_stop":[]}})"));
    assert(!save("{}"));
    assert(!alarm_command_submit("cancel", 2));
    assert(alarm_command_submit("cancel"));
    storage_test_files["/config/alarm_b.json"] = "corrupt";
    alarm_manager_init();
    assert(alarm_snapshot().minute == 30);
    storage_test_files["/config/alarm_a.json"] = "corrupt";
    alarm_manager_init();
    assert(!alarm_snapshot().enabled && alarm_snapshot().storage_error);
    storage_test_files.clear();
    ready = true;
    wall_clock = 1704094140;
    alarm_manager_init();
    std::string hooked(config);
    hooked.replace(hooked.find("\"on_stop\":[]"), strlen("\"on_stop\":[]"), "\"on_stop\":[{\"op\":\"screen.open\",\"args\":{\"target\":\"info\"}}]");
    hooked.replace(hooked.find("\"on_ring\":["), strlen("\"on_ring\":["), "\"on_ring\":[{\"op\":\"screen.open\",\"args\":{\"target\":\"info\"}},");
    assert(save(hooked.c_str()));
    wall_clock += 60;
    alarm_manager_loop();
    assert(alarm_snapshot().state == ALARM_RINGING);
    assert(dispatched.size() == 2 && alarm_snapshot().hook_error);
    assert(alarm_snapshot().next_epoch > wall_clock);
    assert(alarm_command_submit("adjust_minutes", 1, 0));
    alarm_manager_loop();
    assert(alarm_snapshot().state == ALARM_RINGING && dispatched.size() == 2);
    assert(save(hooked.c_str()));
    assert(alarm_snapshot().state == ALARM_RINGING && dispatched.size() == 2);
    std::string edited(hooked);
    edited.replace(edited.find("30,"), 3, "31,");
    storage_test_write_limit = 8;
    assert(!save(edited.c_str()));
    assert(alarm_snapshot().state == ALARM_RINGING && dispatched.size() == 2);
    storage_test_write_limit = SIZE_MAX;
    assert(alarm_command_submit("snooze"));
    alarm_manager_loop();
    assert(alarm_snapshot().state == ALARM_SNOOZED && dispatched.size() == 3);
    assert(alarm_snapshot().snooze_remaining == 540);
    assert(alarm_command_submit("cancel"));
    alarm_manager_loop();
    assert(alarm_snapshot().state == ALARM_IDLE && dispatched.size() == 3);
    wall_clock += 86400;
    alarm_manager_loop();
    assert(alarm_snapshot().state == ALARM_RINGING && dispatched.size() == 5);
    assert(alarm_command_submit("adjust_minutes", 1, 1));
    alarm_manager_loop();
    assert(alarm_snapshot().state == ALARM_IDLE && dispatched.size() == 6);
    assert(dispatched.back() == "Alarm stop:@");
    assert(dispatched_json.back().find("\"op\":\"screen.open\"") != std::string::npos);
    const uint32_t cleanup_generation = last_work_generation;
    assert(last_work_guard(cleanup_generation));
    assert(alarm_command_submit("cancel"));
    alarm_manager_loop();
    assert(last_work_guard(cleanup_generation) && dispatched.size() == 6);
    std::string oversized(hooked);
    oversized.replace(oversized.find("\"info\""), 6, "\"" + std::string(512, 'a') + "\"");
    assert(!save(oversized.c_str()));
    wall_clock += 60;
    ota_after_dispatch = dispatched.size() + 1;
    alarm_manager_loop();
    assert(ota && dispatched.size() == 7 && alarm_snapshot().deferred);
    alarm_manager_loop();
    assert(dispatched.size() == 7);
    ota = false;
    ota_after_dispatch = SIZE_MAX;
    alarm_manager_loop();
    assert(dispatched.size() == 8 && !alarm_snapshot().deferred);
    assert(alarm_command_submit("cancel"));
    ota = true;
    alarm_manager_loop();
    assert(alarm_snapshot().state == ALARM_IDLE && dispatched.size() == 8);
    ota = false;
    alarm_manager_loop();
    assert(dispatched.size() == 9 && dispatched.back() == "Alarm stop:@");
    wall_clock += 86400;
    ota_after_dispatch = dispatched.size() + 1;
    alarm_manager_loop();
    assert(ota && dispatched.size() == 10);
    assert(alarm_command_submit("cancel"));
    alarm_manager_loop();
    ota = false;
    ota_after_dispatch = SIZE_MAX;
    alarm_manager_loop();
    assert(dispatched.size() == 11 && alarm_snapshot().state == ALARM_IDLE);
    wall_clock += 86400;
    ota_after_dispatch = dispatched.size() + 1;
    alarm_manager_loop();
    wall_clock += 301;
    ota = false;
    ota_after_dispatch = SIZE_MAX;
    alarm_manager_loop();
    assert(dispatched.size() == 13 && alarm_snapshot().state == ALARM_IDLE);
    storage_test_files.clear();
    wall_clock = 1704094140;
    monotonic_clock = 0;
    alarm_manager_init();
    assert(save(hooked.c_str()));
    dispatched.clear();
    storage_test_write_limit = 8;
    wall_clock += 60;
    alarm_manager_loop();
    assert(alarm_snapshot().state == ALARM_RINGING && alarm_snapshot().storage_error);
    assert(dispatched.size() == 2);
    const auto failed_history_files = storage_test_files;
    for (int tick = 0; tick < 3; ++tick) alarm_manager_loop();
    assert(dispatched.size() == 2 && storage_test_files == failed_history_files);
    storage_test_write_limit = SIZE_MAX;
    ++timezone_revision;
    alarm_manager_loop();
    assert(alarm_snapshot().state == ALARM_IDLE && dispatched.size() == 3);
    assert(dispatched.back() == "Alarm stop:@");
    storage_test_files.clear();
    wall_clock = 1704094140;
    monotonic_clock = 0;
    ready = false;
    alarm_manager_init();
    assert(save(config));
    diagnostics.clear();
    alarm_manager_loop();
    assert(diagnostics.size() == 1 && has_log("blocked=waiting-ntp"));
    for (int tick = 0; tick < 100; ++tick) alarm_manager_loop();
    assert(diagnostics.size() == 1);
    wall_clock += 60;
    monotonic_clock += 60000;
    alarm_manager_loop();
    assert(diagnostics.size() == 2);
    ready = true;
    alarm_manager_loop();
    assert(has_log("Time readiness=1") && has_log("blocked=rearmed"));
    wall_clock += 60;
    monotonic_clock += 60000;
    alarm_manager_loop();
    assert(has_log("blocked=before-armed-minute"));
    wall_clock += 86400;
    monotonic_clock += 86400000;
    alarm_manager_loop();
    assert(alarm_snapshot().state == ALARM_RINGING);
    assert(has_log("Ring started: reason=schedule"));
    assert(has_log("Alarm ring hook 1/1: dispatch type=@"));
    assert(has_log("Alarm ring hook 1/1: failed result="));
    const size_t log_count = diagnostics.size();
    for (int tick = 0; tick < 100; ++tick) alarm_manager_loop();
    assert(diagnostics.size() == log_count);
    std::puts("alarm manager persistence: PASS");
}