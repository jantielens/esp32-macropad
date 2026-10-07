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

std::map<std::string, std::string> timer_test_files;
bool timer_test_fail_open = false;
size_t timer_test_write_limit = SIZE_MAX;
FakeLittleFS LittleFS;
static time_t wall_clock = 1704094140;
static unsigned long monotonic_clock = 0;
static bool ready = false;
static bool ota = false;
static uint32_t timezone_revision = 0;
static std::vector<std::string> dispatched;
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
bool time_service_localtime(time_t epoch, struct tm* result) { return result && gmtime_r(&epoch, result); }
time_t time_service_alarm_candidate(time_t now, uint8_t hour, uint8_t minute, uint8_t weekdays) {
    struct tm local = {};
    gmtime_r(&now, &local);
    if (!(weekdays & (1U << local.tm_wday))) return 0;
    return now - local.tm_hour * 3600 - local.tm_min * 60 - local.tm_sec + hour * 3600 + minute * 60;
}
void storage_publish_usage(bool) {}
bool ota_activity_is_active() { return ota; }
ActionResult action_dispatch(const ButtonAction& action, const char* label, uint32_t) {
    dispatched.emplace_back(std::string(label) + ":" + action.type);
    if (dispatched.size() == ota_after_dispatch) ota = true;
    return ACTION_FAILED;
}
ActionResult action_dispatch_synchronous(const ButtonAction& action, const char* label,
                                         bool (*guard)(uint32_t), uint32_t generation) {
    last_work_guard = guard;
    last_work_generation = generation;
    return action_dispatch(action, label, 0);
}

static bool save(const char* json) { return alarm_config_save_raw(reinterpret_cast<const uint8_t*>(json), strlen(json)); }
static const char* config = R"({"1":{"enabled":true,"hour":7,"minute":30,"weekdays":62,"snooze_minutes":9,"auto_dismiss_minutes":30,"on_ring":[{"type":"screen","target":"info"}],"on_stop":[]}})";

int main() {
    alarm_manager_init();
    assert(!alarm_snapshot().enabled);
    assert(save(config));
    assert(alarm_snapshot().enabled && alarm_snapshot().minute == 30);
    alarm_manager_init();
    assert(alarm_snapshot().enabled && alarm_snapshot().minute == 30);
    timer_test_write_limit = 8;
    std::string changed(config);
    changed.replace(changed.find("30,"), 3, "31,");
    assert(!save(changed.c_str()));
    assert(alarm_snapshot().minute == 30);
    alarm_manager_init();
    assert(alarm_snapshot().enabled && alarm_snapshot().minute == 30);
    timer_test_write_limit = SIZE_MAX;
    assert(save(changed.c_str()));
    alarm_manager_init();
    assert(alarm_snapshot().minute == 31);
    assert(!save(R"({"1":{"enabled":true,"hour":7,"minute":30,"weekdays":62,"snooze_minutes":9,"auto_dismiss_minutes":30,"on_ring":[{"type":"delay","duration_ms":10}],"on_stop":[]}})"));
    assert(!save(R"({"1":{"enabled":true,"hour":7,"minute":30,"weekdays":62,"snooze_minutes":9,"auto_dismiss_minutes":30,"on_ring":[{"type":"key","key_sequence":"A"}],"on_stop":[]}})"));
    assert(!save("{}"));
    assert(!alarm_command_submit("cancel", 2));
    assert(alarm_command_submit("cancel"));
    timer_test_files["/config/alarm_b.json"] = "corrupt";
    alarm_manager_init();
    assert(alarm_snapshot().minute == 30);
    timer_test_files["/config/alarm_a.json"] = "corrupt";
    alarm_manager_init();
    assert(!alarm_snapshot().enabled && alarm_snapshot().storage_error);
    timer_test_files.clear();
    ready = true;
    wall_clock = 1704094140;
    alarm_manager_init();
    std::string hooked(config);
    hooked.replace(hooked.find("\"on_stop\":[]"), strlen("\"on_stop\":[]"), "\"on_stop\":[{\"type\":\"screen\",\"target\":\"info\"}]");
    hooked.replace(hooked.find("\"on_ring\":["), strlen("\"on_ring\":["), "\"on_ring\":[{\"type\":\"screen\",\"target\":\"info\"},");
    assert(save(hooked.c_str()));
    wall_clock += 60;
    alarm_manager_loop();
    assert(alarm_snapshot().state == ALARM_RINGING);
    assert(dispatched.size() == 2 && alarm_snapshot().hook_error);
    assert(save(hooked.c_str()));
    assert(alarm_snapshot().state == ALARM_RINGING && dispatched.size() == 2);
    std::string edited(hooked);
    edited.replace(edited.find("30,"), 3, "31,");
    timer_test_write_limit = 8;
    assert(!save(edited.c_str()));
    assert(alarm_snapshot().state == ALARM_RINGING && dispatched.size() == 2);
    timer_test_write_limit = SIZE_MAX;
    assert(alarm_command_submit("snooze"));
    alarm_manager_loop();
    assert(alarm_snapshot().state == ALARM_SNOOZED && dispatched.size() == 3);
    assert(alarm_command_submit("cancel"));
    alarm_manager_loop();
    assert(alarm_snapshot().state == ALARM_IDLE && dispatched.size() == 3);
    wall_clock += 86400;
    alarm_manager_loop();
    assert(alarm_snapshot().state == ALARM_RINGING && dispatched.size() == 5);
    assert(save(edited.c_str()));
    assert(alarm_snapshot().state == ALARM_IDLE && dispatched.size() == 6);
    assert(dispatched.back() == "Alarm stop:screen");
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
    assert(dispatched.size() == 9 && dispatched.back() == "Alarm stop:screen");
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
    timer_test_files.clear();
    wall_clock = 1704094140;
    monotonic_clock = 0;
    alarm_manager_init();
    assert(save(hooked.c_str()));
    dispatched.clear();
    timer_test_write_limit = 8;
    wall_clock += 60;
    alarm_manager_loop();
    assert(alarm_snapshot().state == ALARM_RINGING && alarm_snapshot().storage_error);
    assert(dispatched.size() == 2);
    const auto failed_history_files = timer_test_files;
    for (int tick = 0; tick < 3; ++tick) alarm_manager_loop();
    assert(dispatched.size() == 2 && timer_test_files == failed_history_files);
    timer_test_write_limit = SIZE_MAX;
    ++timezone_revision;
    alarm_manager_loop();
    assert(alarm_snapshot().state == ALARM_IDLE && dispatched.size() == 3);
    assert(dispatched.back() == "Alarm stop:screen");
    timer_test_files.clear();
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
    assert(has_log("Time readiness=1") && has_log("blocked=arming-after-sync"));
    wall_clock += 60;
    monotonic_clock += 60000;
    alarm_manager_loop();
    assert(has_log("blocked=before-armed-minute"));
    wall_clock += 86400;
    monotonic_clock += 86400000;
    alarm_manager_loop();
    assert(alarm_snapshot().state == ALARM_RINGING);
    assert(has_log("Ring started: reason=schedule"));
    assert(has_log("Alarm ring hook 1/1: dispatch type=screen"));
    assert(has_log("Alarm ring hook 1/1: failed result="));
    const size_t log_count = diagnostics.size();
    for (int tick = 0; tick < 100; ++tick) alarm_manager_loop();
    assert(diagnostics.size() == log_count);
    std::puts("alarm manager persistence: PASS");
}