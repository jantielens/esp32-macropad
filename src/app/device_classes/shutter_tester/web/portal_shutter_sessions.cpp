#include "portal_shutter_sessions.h"

#if IS_SHUTTER_TESTER

#include "fs_indexed_store.h"
#include "web_portal_fs_store.h"
#include "web_portal_auth.h"
#include "web_portal_json.h"
#include "web_portal_utils.h"
#include "log_manager.h"
#include "component_registry.h"

#include "storage.h"
#include "psram_json_allocator.h"
#include <ArduinoJson.h>
#include <string.h>

#define TAG "SessionsAPI"

// ============================================================================
// Component registration
// ============================================================================

REGISTER_NAV_COMPONENT(shutter_sessions, "shutter-sessions", "camera", "Sessions", 10, "shutter-sessions")

// ============================================================================
// Store instance — shared with shutter_session.cpp via extern declaration
// ============================================================================

// The FsIndexedStore is statically constructed inside shutter_session.cpp.
// We reach it through the public session API for routes that need it;
// GET list/item and DELETE are registered via fs_indexed_store_register_routes().
// The store object itself is declared in shutter_session.cpp and exposed here
// via a getter so there is exactly one FsIndexedStore instance.

// Forward declaration of the getter defined in shutter_session.cpp
extern FsIndexedStore& shutter_session_get_store();

// ============================================================================
// Route registration
// ============================================================================

void web_portal_sessions_register_routes(AsyncWebServer* server) {
    FsIndexedStore& store = shutter_session_get_store();

    // GET, DELETE standard routes (GET list, GET by id, DELETE by id).
    // The generic PATCH from fs_indexed_store_register_routes() is intentionally
    // NOT used: both camera and notes live only in the manifest. We register a
    // custom PATCH below that calls patch_entry() with no data file access.
    //
    // Use the overload that registers GET+DELETE only by omitting PATCH via the
    // standard function — since fs_indexed_store_register_routes registers all four,
    // we register GET+DELETE separately using direct server.on() calls.

    // DELETE /api/sessions — clear all sessions (recovery/reset).
    // Use exact matching so /api/sessions/{id} does NOT fall through here
    // (the default string matcher is BackwardCompatible, which prefix-matches
    // "/api/sessions/" and would route every per-id delete to clear_all()).
    server->on(
        AsyncURIMatcher::exact("/api/sessions"),
        HTTP_DELETE,
        [&store](AsyncWebServerRequest* request) {
            if (!portal_auth_gate(request)) return;
            if (!store.clear_all()) {
                request->send(500, "application/json",
                              "{\"success\":false,\"message\":\"Clear failed\"}");
                return;
            }
            request->send(200, "application/json", "{\"success\":true}");
        }
    );

    // GET /api/sessions/{id} — stream raw data file (must be registered BEFORE
    // the exact-string GET /api/sessions handler).
    server->on(
        AsyncURIMatcher::regex("^/api/sessions/([^/]+)$"),
        HTTP_GET,
        [&store](AsyncWebServerRequest* request) {
            if (!portal_auth_gate(request)) return;
            String id = request->pathArg(0);
            if (id.isEmpty()) {
                request->send(400, "application/json",
                              "{\"success\":false,\"message\":\"Missing id\"}");
                return;
            }
            if (!store.exists(id.c_str())) {
                request->send(404, "application/json",
                              "{\"success\":false,\"message\":\"Not found\"}");
                return;
            }
            String file_path = store.data_path(id.c_str());
            if (!Storage.exists(file_path.c_str())) {
                request->send(404, "application/json",
                              "{\"success\":false,\"message\":\"Not found\"}");
                return;
            }
            sendFileThrottled(request, file_path.c_str(), "application/json");
        }
    );

    // DELETE /api/sessions/{id}
    server->on(
        AsyncURIMatcher::regex("^/api/sessions/([^/]+)$"),
        HTTP_DELETE,
        [&store](AsyncWebServerRequest* request) {
            if (!portal_auth_gate(request)) return;
            String id = request->pathArg(0);
            if (id.isEmpty()) {
                request->send(400, "application/json",
                              "{\"success\":false,\"message\":\"Missing id\"}");
                return;
            }
            if (!store.remove(id.c_str())) {
                request->send(404, "application/json",
                              "{\"success\":false,\"message\":\"Not found\"}");
                return;
            }
            request->send(200, "application/json", "{\"success\":true}");
        }
    );

    // GET /api/sessions — manifest list. Exact match for the same reason as
    // the DELETE clear_all handler above (avoid BackwardCompatible prefix match).
    server->on(
        AsyncURIMatcher::exact("/api/sessions"),
        HTTP_GET,
        [&store](AsyncWebServerRequest* request) {
            if (!portal_auth_gate(request)) return;
            String manifest = store.list();
            if (manifest.isEmpty()) {
                request->send(500, "application/json",
                              "{\"success\":false,\"message\":\"Failed to read manifest\"}");
                return;
            }
            request->send(200, "application/json", manifest);
        }
    );

    // PATCH /api/sessions/{id}
    // Body: { "camera": "...", "notes": "..." } — either or both fields.
    // Both fields are manifest-only: no data file is read or written.
    struct PatchState {
        bool   errored;
        size_t total;
        size_t received;
        char   buf[FS_STORE_PATCH_MAX_BYTES + 1];
        char   id[128];
    };

    server->on(
        AsyncURIMatcher::regex("^/api/sessions/([^/]+)$"),
        HTTP_PATCH,
        [](AsyncWebServerRequest* request) {
            if (request->_tempObject) {
                delete static_cast<PatchState*>(request->_tempObject);
                request->_tempObject = nullptr;
            }
        },
        nullptr,
        [&store](AsyncWebServerRequest* request,
                 uint8_t* data, size_t len,
                 size_t index, size_t total) {
            if (index == 0) {
                if (!portal_auth_gate(request)) return;

                auto* state = new PatchState{};
                state->errored  = false;
                state->total    = total;
                state->received = 0;
                state->id[0]    = '\0';
                request->_tempObject = state;

                String id = request->pathArg(0);
                if (id.isEmpty() || id.length() >= sizeof(state->id)) {
                    state->errored = true;
                    request->send(400, "application/json",
                                  "{\"success\":false,\"message\":\"Missing or invalid id\"}");
                    return;
                }
                strncpy(state->id, id.c_str(), sizeof(state->id) - 1);
                state->id[sizeof(state->id) - 1] = '\0';

                if (total == 0 || total > FS_STORE_PATCH_MAX_BYTES) {
                    state->errored = true;
                    request->send(413, "application/json",
                                  "{\"success\":false,\"message\":\"PATCH body too large\"}");
                    return;
                }
            }

            auto* state = static_cast<PatchState*>(request->_tempObject);
            if (!state || state->errored) return;

            if (state->received + len > FS_STORE_PATCH_MAX_BYTES) {
                state->errored = true;
                request->send(413, "application/json",
                              "{\"success\":false,\"message\":\"PATCH body too large\"}");
                return;
            }
            memcpy(state->buf + state->received, data, len);
            state->received += len;

            if (state->received < state->total) return;

            state->buf[state->received] = '\0';

            BasicJsonDocument<PsramJsonAllocator> body(512);
            auto err = deserializeJson(body, state->buf);
            if (err) {
                request->send(400, "application/json",
                              "{\"success\":false,\"message\":\"Invalid JSON\"}");
                return;
            }

            const char* id = state->id;
            bool ok = true;

            // Both camera and notes live only in the manifest — no data file access needed.
            BasicJsonDocument<PsramJsonAllocator> patch(256);
            if (body.containsKey("camera")) patch["camera"] = body["camera"].as<const char*>();
            if (body.containsKey("notes"))  patch["notes"]  = body["notes"].as<const char*>();
            if (patch.size() > 0 && !store.patch_entry(id, patch.as<JsonObject>())) {
                ok = false;
                LOGW(TAG, "PATCH metadata failed for %s", id);
            }

            if (ok) {
                request->send(200, "application/json", "{\"success\":true}");
            } else {
                request->send(500, "application/json",
                              "{\"success\":false,\"message\":\"Patch failed\"}");
            }
        }
    );

    LOGI(TAG, "Registered /api/sessions routes");
}

#endif // IS_SHUTTER_TESTER
