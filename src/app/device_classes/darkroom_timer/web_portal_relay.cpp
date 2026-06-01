// Relay action configuration REST API — GET/PUT /api/relay.
//
// Uses a top-level /api/relay namespace (not /api/config/relay) because the
// core /api/config GET route is a prefix matcher and would otherwise swallow
// the GET — mirrors the per-device-class /api/scale, /api/brews, /api/shutter
// namespacing convention.
//
// Self-registers its AsyncWebServer routes via REGISTER_ROUTES() (see
// web_portal_routes.h), mirroring the shutter-tester web route modules. The
// static initializer runs because this file is #include-aggregated into
// route_components.cpp under #if IS_DARKROOM_TIMER.

#include "board_config.h"

#if IS_DARKROOM_TIMER

#include "log_manager.h"
#include "relay_controller.h"
#include "web_portal_auth.h"
#include "web_portal_routes.h"

#include <ESPAsyncWebServer.h>

#define TAG "RelayAPI"

static void handleGetRelayConfig(AsyncWebServerRequest* request) {
    if (!portal_auth_gate(request)) return;
    String output;
    if (relay_get_config_json(output)) {
        request->send(200, "application/json", output);
    } else {
        request->send(500, "application/json", "{\"error\":\"failed to serialize\"}");
    }
}

// Maximum accepted relay-config body. 4 relay slots of ShellyPayload plus JSON
// overhead stays well under this; anything larger is rejected.
#define RELAY_POST_MAX_BYTES 4096

// Per-request accumulator for the relay-config PUT body. AsyncWebServer may
// deliver the body in multiple chunks once the payload approaches the MTU, so
// we buffer chunks on the request and parse once on the final chunk. Mirrors
// the per-request _tempObject pattern in web_portal_fs_store.cpp.
struct RelayPostState {
    bool   errored;
    size_t total;
    size_t received;
    uint8_t buf[RELAY_POST_MAX_BYTES + 1];
};

static void handlePutRelayConfigBody(AsyncWebServerRequest* request, uint8_t* data,
                                     size_t len, size_t index, size_t total) {
    if (index == 0) {
        if (!portal_auth_gate(request)) return;

        auto* state = new RelayPostState{};
        state->errored  = false;
        state->total    = total;
        state->received = 0;
        request->_tempObject = state;

        if (total == 0 || total > RELAY_POST_MAX_BYTES) {
            state->errored = true;
            request->send(413, "application/json", "{\"error\":\"payload too large\"}");
            return;
        }
    }

    auto* state = static_cast<RelayPostState*>(request->_tempObject);
    if (!state || state->errored) return;

    if (state->received + len > RELAY_POST_MAX_BYTES) {
        state->errored = true;
        request->send(413, "application/json", "{\"error\":\"payload too large\"}");
        return;
    }
    memcpy(state->buf + state->received, data, len);
    state->received += len;

    if (state->received < state->total) return;  // more chunks to come

    state->buf[state->received] = '\0';
    bool ok = relay_save_config_from_json(state->buf, state->received);
    request->send(ok ? 200 : 500, "application/json",
                  ok ? "{\"ok\":true}" : "{\"error\":\"save failed\"}");
}

static void relay_register(AsyncWebServer* server) {
    server->on("/api/relay", HTTP_GET, handleGetRelayConfig);
    server->on("/api/relay", HTTP_PUT,
               // Request handler fires after all body chunks — frees per-request state.
               [](AsyncWebServerRequest* request) {
                   if (request->_tempObject) {
                       delete static_cast<RelayPostState*>(request->_tempObject);
                       request->_tempObject = nullptr;
                   }
               },
               nullptr, handlePutRelayConfigBody);
    LOGI(TAG, "Registered relay config routes");
}

REGISTER_ROUTES(relay_register)

#endif // IS_DARKROOM_TIMER
