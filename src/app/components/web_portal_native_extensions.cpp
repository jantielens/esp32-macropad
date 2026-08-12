#include "../board_config.h"

#if HAS_NATIVE_EXTENSIONS

#include "../native_extension.h"
#include "../storage.h"
#include "../web_portal_auth.h"
#include "../web_portal_json.h"
#include "../web_portal_routes.h"

#include <string.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

namespace {

struct ExtensionUploadState {
    File file;
    uint8_t slot;
    size_t total;
    size_t received;
    char filename[64];
    char temporary[64];
};

portMUX_TYPE s_upload_lock = portMUX_INITIALIZER_UNLOCKED;
bool s_slot_uploading[NATIVE_EXTENSION_SLOT_COUNT] = {};

void upload_cleanup(AsyncWebServerRequest* request, bool remove_temporary = true) {
    auto* state = request ? static_cast<ExtensionUploadState*>(request->_tempObject) : nullptr;
    if (!state) return;
    request->_tempObject = nullptr;
    if (state->file) state->file.close();
    if (remove_temporary && state->temporary[0]) Storage.remove(state->temporary);
    portENTER_CRITICAL(&s_upload_lock);
    s_slot_uploading[state->slot] = false;
    portEXIT_CRITICAL(&s_upload_lock);
    delete state;
}

bool upload_start(AsyncWebServerRequest* request, size_t total) {
    if (!request->hasParam("slot") || !request->hasParam("filename") || total == 0) {
        web_portal_send_json_error(request, 400, "Slot, filename, and package body are required");
        return false;
    }
    const long slot_value = request->getParam("slot")->value().toInt();
    if (slot_value < 0 || slot_value >= native_extension_slot_count()) {
        web_portal_send_json_error(request, 400, "Invalid extension slot");
        return false;
    }
    const uint8_t slot = static_cast<uint8_t>(slot_value);
    portENTER_CRITICAL(&s_upload_lock);
    const bool busy = s_slot_uploading[slot];
    if (!busy) s_slot_uploading[slot] = true;
    portEXIT_CRITICAL(&s_upload_lock);
    if (busy) {
        web_portal_send_json_error(request, 409, "Extension slot upload already in progress");
        return false;
    }
    if (!Storage.exists("/extensions") && !Storage.mkdir("/extensions")) {
        portENTER_CRITICAL(&s_upload_lock);
        s_slot_uploading[slot] = false;
        portEXIT_CRITICAL(&s_upload_lock);
        web_portal_send_json_error(request, 500, "Unable to create extension storage");
        return false;
    }
    auto* state = new ExtensionUploadState{};
    if (!state) {
        portENTER_CRITICAL(&s_upload_lock);
        s_slot_uploading[slot] = false;
        portEXIT_CRITICAL(&s_upload_lock);
        web_portal_send_json_error(request, 500, "Unable to start extension upload");
        return false;
    }
    state->slot = slot;
    state->total = total;
    strlcpy(state->filename, request->getParam("filename")->value().c_str(), sizeof(state->filename));
    snprintf(state->temporary, sizeof(state->temporary), "/extensions/upload-%u-%08lx.tmp", slot,
             static_cast<unsigned long>(esp_random()));
    state->file = Storage.open(state->temporary, "w");
    if (!state->file) {
        portENTER_CRITICAL(&s_upload_lock);
        s_slot_uploading[slot] = false;
        portEXIT_CRITICAL(&s_upload_lock);
        delete state;
        web_portal_send_json_error(request, 500, "Unable to create extension upload");
        return false;
    }
    request->_tempObject = state;
    request->onDisconnect([request]() { upload_cleanup(request); });
    return true;
}

void handle_extension_status(AsyncWebServerRequest* request) {
    if (!portal_auth_gate(request)) return;
    AsyncResponseStream* response = request->beginResponseStream("application/json");
    response->print("{\"slots\":[");
    for (uint8_t slot = 0; slot < native_extension_slot_count(); ++slot) {
        NativeExtensionSlotInfo info = {};
        native_extension_get_slot(slot, &info);
        if (slot) response->print(',');
        response->printf("{\"slot\":%u,\"installed\":%s,\"staged\":%s,\"pending_delete\":%s,\"incompatible_abi\":%s,\"enabled\":%s,\"loaded\":%s,\"capacity\":%u,\"size\":%u,\"staged_size\":%u,\"abi_version\":%u,\"id\":\"%s\",\"version\":\"%s\",\"target_abi\":\"%s\",\"title\":\"%s\",\"runtime_state\":%u,\"runtime_detail\":\"%s\"}", info.slot, info.installed ? "true" : "false", info.staged ? "true" : "false", info.pending_delete ? "true" : "false", info.incompatible_abi ? "true" : "false", info.enabled ? "true" : "false", info.loaded ? "true" : "false", info.capacity, info.elf_size, info.staged_size, info.abi_version, info.id, info.version, info.target_abi, info.title, info.runtime_state, info.runtime_detail);
    }
    response->print("]}");
    request->send(response);
}

void native_extension_routes_register(AsyncWebServer* server) {
    server->on("/api/extensions", HTTP_GET, handle_extension_status);
    server->on("/api/extensions/upload", HTTP_POST,
               [](AsyncWebServerRequest* request) {
                   if (!portal_auth_gate(request)) return;
               }, nullptr,
               [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
                   if (!portal_auth_gate(request)) return;
                   if (index == 0) {
                       if (!upload_start(request, total)) return;
                   }
                   auto* state = static_cast<ExtensionUploadState*>(request->_tempObject);
                   if (!state || state->total != total || index != state->received ||
                       len > state->total - state->received || (len > 0 && !data) ||
                       state->file.write(data, len) != len) {
                       upload_cleanup(request);
                       web_portal_send_json_error(request, 400, "Extension upload failed");
                       return;
                   }
                   state->received += len;
                   if (state->received != state->total) return;
                   state->file.close();
                   const bool staged = native_extension_stage_file(state->slot, state->filename, state->temporary);
                   upload_cleanup(request);
                   if (!staged) {
                       web_portal_send_json_error(request, 400, "Invalid extension package");
                       return;
                   }
                   request->send(201, "application/json", "{\"success\":true}");
               });
    server->on("/api/extensions", HTTP_DELETE, [](AsyncWebServerRequest* request) {
        if (!portal_auth_gate(request)) return;
        if (!request->hasParam("slot") || !native_extension_delete(static_cast<uint8_t>(request->getParam("slot")->value().toInt()))) {
            web_portal_send_json_error(request, 400, "Invalid extension slot"); return;
        }
        request->send(200, "application/json", "{\"success\":true,\"message\":\"Extension deleted; reboot required\"}");
    });
    server->on("/api/extensions/enabled", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!portal_auth_gate(request)) return;
        if (!request->hasParam("slot") || !request->hasParam("enabled") ||
            !native_extension_set_enabled(static_cast<uint8_t>(request->getParam("slot")->value().toInt()),
                                          request->getParam("enabled")->value() == "true")) {
            web_portal_send_json_error(request, 400, "Invalid extension slot"); return;
        }
        request->send(200, "application/json", "{\"success\":true,\"message\":\"Extension updated; reboot required\"}");
    });
}

} // namespace

REGISTER_ROUTES(native_extension_routes_register)

#endif