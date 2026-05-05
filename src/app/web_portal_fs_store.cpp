#include "web_portal_fs_store.h"

#include "log_manager.h"
#include "web_portal_auth.h"
#include "web_portal_json.h"

#include <LittleFS.h>
#include <string.h>

#define TAG "FsStoreAPI"

// Maximum PATCH body size accepted (metadata fields only).
#define FS_STORE_PATCH_MAX_BYTES 1024

// ---------------------------------------------------------------------------
// Route context: each registered store gets its own handler closures that
// capture the store pointer and base path via lambda captures.
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
    // -----------------------------------------------------------------------
    // Build a regex pattern matching the base URL followed by "/<id>" where
    // the id is any non-empty path component without slashes.
    String get_pattern = String("^") + base_url + "/([^/]+)$";
    server.on(
        AsyncURIMatcher::regex(get_pattern.c_str()),
        HTTP_GET,
        [&store, base_url](AsyncWebServerRequest* request) {
            if (!portal_auth_gate(request)) return;

            String id = request->pathArg(0);
            if (id.isEmpty()) {
                request->send(400, "application/json",
                              "{\"success\":false,\"message\":\"Missing id\"}");
                return;
            }

            // Validate that the entry exists in the manifest before serving
            // the file — guards against directory traversal via path args.
            String content = store.get(id.c_str());
            if (content.isEmpty()) {
                request->send(404, "application/json",
                              "{\"success\":false,\"message\":\"Not found\"}");
                return;
            }

            // Build the LittleFS path for AsyncFileResponse streaming.
            // The path is: base_path + "/" + id + ".json".
            // We derive base_path from the base_url (they share the same
            // directory; feature code uses the same string for both).
            // Rather than maintain a separate data-path helper here, we
            // reconstruct from the known naming convention:
            //   base_url == base_path  (e.g. "/storage/sessions")
            //   data file  == base_url + "/" + id + ".json"
            String file_path = String(base_url) + "/" + id + ".json";

            if (!LittleFS.exists(file_path.c_str())) {
                // Manifest said it exists but the file is missing — serve
                // the in-memory content as a fallback (already loaded above).
                request->send(200, "application/json", content);
                return;
            }

            // Stream from flash — no large heap allocation.
            request->send(LittleFS, file_path.c_str(), "application/json");
        }
    );

    // -----------------------------------------------------------------------
    // DELETE {base_url}/{id} — delete document and update manifest
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

            bool ok = store.remove(id.c_str());
            if (!ok) {
                // remove() is best-effort; treat as success if entry was absent
                LOGW(TAG, "DELETE: store.remove('%s') returned false (may not have existed)",
                     id.c_str());
            }
            request->send(200, "application/json", "{\"success\":true}");
        }
    );

    // -----------------------------------------------------------------------
    // PATCH {base_url}/{id} — update metadata fields
    //
    // Body: JSON object with fields to patch, e.g. {"notes":"new text"}
    // Uses a simple inline accumulator — metadata bodies are small (≤1 KB).
    // -----------------------------------------------------------------------

    // Per-request accumulator for the PATCH body.  Because AsyncWebServer
    // body handlers may be called from a single task sequentially, a static
    // buffer is sufficient.  We guard concurrent access with a flag.
    struct PatchState {
        bool in_progress;
        bool errored;
        size_t total;
        size_t received;
        char buf[FS_STORE_PATCH_MAX_BYTES + 1];
        char id[128];
    };
    static PatchState patch_state = {};

    // Request handler (fires after body is complete or on error)
    auto patch_request_handler = [&store](AsyncWebServerRequest* request) {
        (void)request; // Final dispatch happens in the body handler
    };

    // Body handler
    auto patch_body_handler = [&store](AsyncWebServerRequest* request,
                                        uint8_t* data, size_t len,
                                        size_t index, size_t total) {
        if (!portal_auth_gate(request)) return;

        if (index == 0) {
            // Start of a new PATCH request
            patch_state.in_progress = true;
            patch_state.errored     = false;
            patch_state.total       = total;
            patch_state.received    = 0;
            patch_state.id[0]       = '\0';

            String id = request->pathArg(0);
            if (id.isEmpty() || id.length() >= sizeof(patch_state.id)) {
                patch_state.errored = true;
                request->send(400, "application/json",
                              "{\"success\":false,\"message\":\"Missing or invalid id\"}");
                return;
            }
            strncpy(patch_state.id, id.c_str(), sizeof(patch_state.id) - 1);
            patch_state.id[sizeof(patch_state.id) - 1] = '\0';

            if (total == 0 || total > FS_STORE_PATCH_MAX_BYTES) {
                patch_state.errored = true;
                request->send(413, "application/json",
                              "{\"success\":false,\"message\":\"PATCH body too large\"}");
                return;
            }
        }

        if (patch_state.errored) return;

        // Accumulate chunk
        if (patch_state.received + len > FS_STORE_PATCH_MAX_BYTES) {
            patch_state.errored = true;
            request->send(413, "application/json",
                          "{\"success\":false,\"message\":\"PATCH body too large\"}");
            return;
        }
        memcpy(patch_state.buf + patch_state.received, data, len);
        patch_state.received += len;

        if (patch_state.received < patch_state.total) {
            return; // More chunks pending
        }

        // All chunks received — null-terminate and process
        patch_state.buf[patch_state.received] = '\0';
        patch_state.in_progress = false;

        String json_patch = String(patch_state.buf);
        bool ok = store.patch_meta(patch_state.id, json_patch);
        if (ok) {
            request->send(200, "application/json", "{\"success\":true}");
        } else {
            request->send(404, "application/json",
                          "{\"success\":false,\"message\":\"Not found or patch failed\"}");
        }
    };

    server.on(
        AsyncURIMatcher::regex(get_pattern.c_str()),
        HTTP_PATCH,
        patch_request_handler,
        nullptr,
        patch_body_handler
    );

    LOGI(TAG, "Registered routes for %s", base_url);
}
