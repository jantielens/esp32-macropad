#include "component_registry.h"
#include "alarm_manager.h"
#include "main_loop_bridge.h"
#include "config_psram.h"
#include "web_portal_json.h"
#include <stdlib.h>
#include <string.h>

namespace {
struct AlarmSaveJob { mutable uint8_t* data; size_t length; bool scoped; AlarmConfigSection section; };
void alarm_save_cleanup(const void* context) {
    free(static_cast<const AlarmSaveJob*>(context)->data);
}
void alarm_save_execute(const void* context, bool* ok, char* message, size_t length) {
    const auto* job = static_cast<const AlarmSaveJob*>(context);
    *ok = job->scoped ? alarm_config_save_section_raw(job->data, job->length, job->section)
        : alarm_config_save_raw(job->data, job->length);
    strlcpy(message, *ok ? "saved" : alarm_last_error(), length);
    free(job->data);
    job->data = nullptr;
}
bool alarm_save_bridge(const uint8_t* data, size_t length, bool scoped = false,
                       AlarmConfigSection section = AlarmConfigSection::Schedule) {
    AlarmSaveJob job{static_cast<uint8_t*>(config_psram_alloc(length, "alarm_save")), length, scoped, section};
    if (!job.data) return false;
    memcpy(job.data, data, length);
    bool ok = false;
    char message[128];
    LoopBridgeResult result = loop_bridge_dispatch(alarm_save_execute, &job, sizeof(job), 10000, &ok,
        message, sizeof(message), alarm_save_cleanup);
    if (result != LOOP_BRIDGE_OK && result != LOOP_BRIDGE_TIMEOUT) free(job.data);
    return result == LOOP_BRIDGE_OK && ok;
}
void alarms_get_config(AsyncWebServerRequest* request) {
    auto document = make_psram_json_doc(16384);
    alarm_config_to_json(document->to<JsonObject>());
    web_portal_send_json_chunked(request, document);
}
void alarms_save_config(AsyncWebServerRequest* request, uint8_t* data, size_t length, size_t index, size_t total) {
    component_handle_save_body(request, data, length, index, total,
        [](const uint8_t* body, size_t size) { return alarm_save_bridge(body, size); }, 8192);
}
void alarms_save_schedule(AsyncWebServerRequest* request, uint8_t* data, size_t length, size_t index, size_t total) {
    component_handle_save_body(request, data, length, index, total,
        [](const uint8_t* body, size_t size) { return alarm_save_bridge(body, size, true, AlarmConfigSection::Schedule); }, 8192);
}
void alarms_save_behavior(AsyncWebServerRequest* request, uint8_t* data, size_t length, size_t index, size_t total) {
    component_handle_save_body(request, data, length, index, total,
        [](const uint8_t* body, size_t size) { return alarm_save_bridge(body, size, true, AlarmConfigSection::Behavior); }, 8192);
}
void alarms_get_status(AsyncWebServerRequest* request) {
    auto document = make_psram_json_doc(1024);
    alarm_status_to_json(document->to<JsonObject>());
    web_portal_send_json_chunked(request, document);
}
void alarms_cancel(AsyncWebServerRequest* request) {
    const bool ok = alarm_command_submit("cancel");
    request->send(ok ? 200 : 503, "application/json", ok ? "{\"success\":true}" : "{\"success\":false}");
}
void alarms_snooze(AsyncWebServerRequest* request) {
    const bool ok = alarm_command_submit("snooze");
    request->send(ok ? 200 : 503, "application/json", ok ? "{\"success\":true}" : "{\"success\":false}");
}
const ComponentAction alarms_actions[] = {
    {"schedule", HTTP_POST, nullptr, alarms_save_schedule},
    {"behavior", HTTP_POST, nullptr, alarms_save_behavior},
    {"status", HTTP_GET, alarms_get_status, nullptr},
    {"cancel", HTTP_POST, alarms_cancel, nullptr},
    {"snooze", HTTP_POST, alarms_snooze, nullptr},
};
}
static ComponentDef alarms_component = {
    .id = "alarms",
    .category = "alarm",
    .display_name = "Schedule",
    .nav_order = 10,
    .get_config = alarms_get_config,
    .save_config = nullptr,
    .save_config_body = alarms_save_config,
    .delete_config = nullptr,
    .custom_actions = alarms_actions,
    .num_custom_actions = sizeof(alarms_actions) / sizeof(alarms_actions[0]),
    .fragment_id = "alarm-schedule",
    .portal_script = "portal_alarms.js",
    .portal_style = nullptr,
};
REGISTER_COMPONENT(alarms);
static ComponentDef alarm_behavior_component = {
    .id = "alarm-behavior",
    .category = "alarm",
    .display_name = "Behavior",
    .nav_order = 20,
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = nullptr,
    .num_custom_actions = 0,
    .fragment_id = "alarm-behavior",
    .portal_script = "portal_alarms.js",
    .portal_style = nullptr,
};
REGISTER_COMPONENT(alarm_behavior);