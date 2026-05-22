#include "web_portal_fs_store.h"
#include "board_config.h"

#if HAS_DISPLAY

#include "log_manager.h"
#include "web_portal_auth.h"
#include "web_portal_json.h"
#include "web_portal_utils.h"

#include "storage.h"
#include <string.h>

#define TAG "FsStoreAPI"

// ---------------------------------------------------------------------------
// Route context: each registered store gets its own handler closures that
// capture the store reference via lambda captures.
// ---------------------------------------------------------------------------

void fs_indexed_store_register_routes(AsyncWebServer& server,
                                      FsIndexedStore& store,
                                      const char* base_url) {
    // -----------------------------------------------------------------------
    // GET {base_url} — return manifest JSON (fast, served from RAM cache)
    // -----------------------------------------------------------------------
    String list_url = String(base_url);
    server.on(list_url.c_str(), HTTP_GET,
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

    // -----------------------------------------------------------------------
    // GET {base_url}/{id} — stream full document from flash (no heap spike)
    //
    // Uses exists() for a manifest-only validation (no file I/O), then
    // streams via AsyncFileResponse — avoids loading 30-50 KB into RAM.
    // -----------------------------------------------------------------------
    String get_pattern = String("^") + base_url + "/([^/]+)$";
    server.on(
        AsyncURIMatcher::regex(get_pattern.c_str()),
        HTTP_GET,
        [&store](AsyncWebServerRequest* request) {
            if (!portal_auth_gate(request)) return;

            String id = request->pathArg(0);
            if (id.isEmpty()) {
                request->send(400, "application/json",
                              "{\"success\":false,\"message\":\"Missing id\"}");
                return;
            }

            // Manifest-only existence check — guards against directory traversal;
            // no file I/O, no heap allocation for the data payload.
            if (!store.exists(id.c_str())) {
                request->send(404, "application/json",
                              "{\"success\":false,\"message\":\"Not found\"}");
                return;
            }

            String file_path = store.data_path(id.c_str());
            if (!Storage.exists(file_path.c_str())) {
                // Manifest has the entry but data file is gone (orphan).
                request->send(404, "application/json",
                              "{\"success\":false,\"message\":\"Not found\"}");
                return;
            }

            // Stream from flash — throttled to prevent WiFi TX buffer
            // exhaustion on ESP-Hosted (SDIO).
            sendFileThrottled(request, file_path.c_str(), "application/json");
        }
    );

    // -----------------------------------------------------------------------
    // DELETE {base_url}/{id} — delete document and update manifest
    // Returns 404 if the entry does not exist.
    // -----------------------------------------------------------------------
    server.on(
        AsyncURIMatcher::regex(get_pattern.c_str()),
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

    // -----------------------------------------------------------------------
    // PATCH {base_url}/{id} — update metadata fields
    //
    // Body: JSON object with fields to patch, e.g. {"notes":"new text"}
    //
    // Per-request state is heap-allocated and attached to the request via
    // _tempObject, so concurrent PATCH requests (or multiple store
    // registrations) never share a buffer.
    // -----------------------------------------------------------------------

    struct PatchState {
        bool   errored;
        size_t total;
        size_t received;
        char   buf[FS_STORE_PATCH_MAX_BYTES + 1];
        char   id[128];
    };

    server.on(
        AsyncURIMatcher::regex(get_pattern.c_str()),
        HTTP_PATCH,
        // Request handler fires after all body chunks — frees per-request state.
        [](AsyncWebServerRequest* request) {
            if (request->_tempObject) {
                delete static_cast<PatchState*>(request->_tempObject);
                request->_tempObject = nullptr;
            }
        },
        nullptr,
        // Body handler — accumulates chunks into per-request heap state.
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

            // All chunks received — process the patch.
            state->buf[state->received] = '\0';
            String json_patch(state->buf);
            bool ok = store.patch_meta(state->id, json_patch);

            if (ok) {
                request->send(200, "application/json", "{\"success\":true}");
            } else {
                request->send(404, "application/json",
                              "{\"success\":false,\"message\":\"Not found or patch failed\"}");
            }
        }
    );

    LOGI(TAG, "Registered routes for %s", base_url);
}

#endif // HAS_DISPLAY
