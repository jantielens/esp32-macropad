#include "../board_config.h"

#if HAS_NATIVE_EXTENSIONS

#include "../component_registry.h"
#include "../native_extension.h"
#include "../storage.h"
#include "../web_portal_auth.h"
#include "../web_portal_json.h"
#include "../web_portal_routes.h"

#include <string.h>

namespace {

uint8_t s_stage_slot = 0;
char s_stage_filename[64] = {};

bool save_staged_extension(const uint8_t* data, size_t len) {
    return native_extension_stage(s_stage_slot, s_stage_filename, data, len);
}

void handle_extension_status(AsyncWebServerRequest* request) {
    if (!portal_auth_gate(request)) return;
    AsyncResponseStream* response = request->beginResponseStream("application/json");
    response->print("{\"slots\":[");
    for (uint8_t slot = 0; slot < native_extension_slot_count(); ++slot) {
        NativeExtensionSlotInfo info = {};
        native_extension_get_slot(slot, &info);
        if (slot) response->print(',');
        response->printf("{\"slot\":%u,\"installed\":%s,\"staged\":%s,\"pending_delete\":%s,\"incompatible_abi\":%s,\"enabled\":%s,\"loaded\":%s,\"capacity\":%u,\"size\":%u,\"staged_size\":%u,\"abi_version\":%u,\"id\":\"%s\",\"version\":\"%s\"}", info.slot, info.installed ? "true" : "false", info.staged ? "true" : "false", info.pending_delete ? "true" : "false", info.incompatible_abi ? "true" : "false", info.enabled ? "true" : "false", info.loaded ? "true" : "false", info.capacity, info.elf_size, info.staged_size, info.abi_version, info.id, info.version);
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
                       if (!request->hasParam("slot") || !request->hasParam("filename")) {
                           web_portal_send_json_error(request, 400, "Slot and filename are required");
                           return;
                       }
                       s_stage_slot = static_cast<uint8_t>(request->getParam("slot")->value().toInt());
                       strlcpy(s_stage_filename, request->getParam("filename")->value().c_str(), sizeof(s_stage_filename));
                   }
                   component_handle_save_body(request, data, len, index, total,
                                              save_staged_extension, 128 * 1024);
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