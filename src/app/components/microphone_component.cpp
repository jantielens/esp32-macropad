#include "component_registry.h"
#include "audio_input.h"
#include "bounded_psram_json_allocator.h"
#include "microphone_config_json.h"
#include "ota_activity.h"
#include "web_portal_live_microphone.h"
#include "web_portal_state.h"
#include <esp_heap_caps.h>
#include <stdio.h>
#include <string.h>

namespace {
struct MicrophoneSettingsBody {
    size_t received;
    size_t total;
    uint8_t data[256];
};

void microphone_settings_cleanup(AsyncWebServerRequest* request) {
    heap_caps_free(request->_tempObject);
    request->_tempObject = nullptr;
}

void microphone_settings_body(AsyncWebServerRequest* request, uint8_t* data,
                              size_t len, size_t index, size_t total) {
    if (request->isSent()) return;
    if (web_portal_is_recovery_ap_active() || ota_activity_is_active()) {
        microphone_settings_cleanup(request);
        request->send(web_portal_is_recovery_ap_active() ? 404 : 409, "application/json",
                      "{\"success\":false,\"message\":\"Microphone settings unavailable\"}");
        return;
    }
    if (index == 0) {
        if (!total || total > sizeof(MicrophoneSettingsBody::data)) {
            request->send(413, "application/json", "{\"success\":false,\"message\":\"Settings body too large\"}");
            return;
        }
        auto* body = static_cast<MicrophoneSettingsBody*>(
            heap_caps_malloc(sizeof(MicrophoneSettingsBody), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!body) {
            request->send(503, "application/json", "{\"success\":false,\"message\":\"Not enough PSRAM for settings\"}");
            return;
        }
        body->received = 0;
        body->total = total;
        request->_tempObject = body;
        request->onDisconnect([request]() { microphone_settings_cleanup(request); });
    }
    auto* body = static_cast<MicrophoneSettingsBody*>(request->_tempObject);
    if (!body || total != body->total || index != body->received ||
        index > total || len > total - index || (len && !data)) {
        microphone_settings_cleanup(request);
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid settings body\"}");
        return;
    }
    memcpy(body->data + index, data, len);
    body->received += len;
    if (body->received != total) return;

    BoundedPsramJsonAllocator allocator(8192);
    JsonDocument doc(&allocator);
    AudioInputProcessingSettings settings = audio_input_get_processing();
    const DeserializationError error = deserializeJson(doc, body->data, total);
    const bool valid = !error &&
        doc.is<JsonObject>() &&
        (!doc["microphone_gain_db"].isUnbound() || !doc["microphone_reduce_rumble"].isUnbound()) &&
        microphone_parse_settings(doc.as<JsonObjectConst>(), &settings.gain_db, &settings.reduce_rumble);
    microphone_settings_cleanup(request);
    if (error == DeserializationError::NoMemory) {
        request->send(503, "application/json", "{\"success\":false,\"message\":\"Not enough PSRAM for settings\"}");
        return;
    }
    if (!valid || !audio_input_set_processing(settings)) {
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid microphone settings\"}");
        return;
    }
    request->send(200, "application/json", "{\"success\":true}");
}

void microphone_levels(AsyncWebServerRequest* request) {
    if (web_portal_is_recovery_ap_active()) {
        request->send(404, "application/json", "{\"error\":\"Unavailable in recovery mode\"}");
        return;
    }
    if (!request->hasHeader("X-Microphone-Test")) {
        request->send(403, "application/json", "{\"error\":\"Browser request required\"}");
        return;
    }
    audio_input_meter_request();
    const AudioInputDiagnostics d = audio_input_diagnostics();
    char body[256];
    snprintf(body, sizeof(body),
             "{\"available\":%s,\"ota\":%s,\"in_use\":%s,\"fresh\":%s,\"sampled\":%s,"
             "\"rms\":%u,\"peak\":%u,\"input_full_scale\":%s,\"gain_clipped\":%s}",
             d.available ? "true" : "false", d.ota_active ? "true" : "false",
             d.in_use ? "true" : "false", d.fresh ? "true" : "false", d.sampled ? "true" : "false",
             static_cast<unsigned>(d.rms), static_cast<unsigned>(d.peak),
             d.input_full_scale ? "true" : "false", d.gain_clipped ? "true" : "false");
    AsyncWebServerResponse* response = request->beginResponse(200, "application/json", body);
    if (!response) {
        request->send(503, "application/json", "{\"error\":\"Cannot create response\"}");
        return;
    }
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
}

const ComponentAction microphone_actions[] = {
    {"settings", HTTP_PUT, nullptr, microphone_settings_body},
    {"levels", HTTP_GET, microphone_levels, nullptr},
#if HAS_MICROPHONE_STREAMING
    {"stream", HTTP_GET, handleLiveMicrophoneStream, nullptr},
#endif
};
}

static ComponentDef microphone_component = {
    .id = "microphone",
    .category = "audio",
    .display_name = "Microphone",
    .nav_order = 45,
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = microphone_actions,
    .num_custom_actions = sizeof(microphone_actions) / sizeof(microphone_actions[0]),
    .fragment_id = "microphone",
    .portal_script = "/portal-microphone.js",
    .portal_style = nullptr,
};
REGISTER_COMPONENT(microphone)
