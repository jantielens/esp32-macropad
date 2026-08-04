#include "web_portal_brews.h"

#if IS_COFFEE_SCALE

#include "../brew/brew_log.h"
#include "log_manager.h"
#include "storage.h"
#include "web_portal_auth.h"
#include "web_portal_cors.h"
#include "web_portal_routes.h"
#include "web_portal_json.h"

#include <ArduinoJson.h>
#include <esp_heap_caps.h>

#define TAG "BrewAPI"

// ============================================================================
// GET /api/brews — list all brews (newest first, fields only)
// Optional query param: id=N for single full brew
// ============================================================================

void handleGetBrews(AsyncWebServerRequest* request) {
    // Check for single brew request: /api/brews?id=N
    if (request->hasParam("id")) {
        handleGetBrew(request);
        return;
    }

    File dir = Storage.open(BREW_LOG_DIR);
    if (!dir || !dir.isDirectory()) {
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        response->print("{\"brews\":[],\"count\":0,\"max\":");
        response->print(BREW_LOG_MAX_BREWS);
        response->print("}");
        web_portal_add_cors_headers(response);
        request->send(response);
        return;
    }

    // Collect IDs first to sort newest-first
    uint16_t ids[BREW_LOG_MAX_BREWS];
    uint16_t count = 0;

    File f = dir.openNextFile();
    while (f && count < BREW_LOG_MAX_BREWS) {
        if (!f.isDirectory()) {
            unsigned id = 0;
            if (sscanf(f.name(), "%u.json", &id) == 1) {
                ids[count++] = (uint16_t)id;
            }
        }
        f = dir.openNextFile();
    }

    // Sort descending (newest first)
    for (uint16_t i = 0; i < count; i++) {
        for (uint16_t j = i + 1; j < count; j++) {
            if (ids[j] > ids[i]) {
                uint16_t tmp = ids[i];
                ids[i] = ids[j];
                ids[j] = tmp;
            }
        }
    }

    // Stream response
    AsyncResponseStream* response = request->beginResponseStream("application/json");
    web_portal_add_cors_headers(response);
    response->print("{\"brews\":[");

    for (uint16_t i = 0; i < count; i++) {
        char path[32];
        snprintf(path, sizeof(path), "%s/%04u.json", BREW_LOG_DIR, (unsigned)ids[i]);

        File bf = Storage.open(path, "r");
        if (!bf) continue;

        // Read file and parse to extract fields only (skip series)
        size_t sz = bf.size();
        if (sz > 8192) { bf.close(); continue; }  // skip abnormally large files

        // Read into a temporary buffer
        char* buf = (char*)malloc(sz + 1);
        if (!buf) { bf.close(); continue; }
        bf.readBytes(buf, sz);
        buf[sz] = '\0';
        bf.close();

        // Parse just to extract fields + template_info
        StaticJsonDocument<4096> doc;
        DeserializationError err = deserializeJson(doc, buf, sz);
        free(buf);
        if (err) continue;

        if (i > 0) response->print(',');
        response->printf("{\"id\":%u,\"v\":%d,\"fields\":",
                         (unsigned)ids[i],
                         doc["v"] | 1);
        // Serialize just the fields array
        serializeJson(doc["fields"], *response);
        // Include template_info if present (for display_name in list view)
        if (doc.containsKey("template_info")) {
            response->print(",\"template_info\":");
            serializeJson(doc["template_info"], *response);
        }
        response->print('}');
    }

    response->printf("],\"count\":%u,\"max\":%u}", (unsigned)count, (unsigned)BREW_LOG_MAX_BREWS);
    request->send(response);
}

// ============================================================================
// GET /api/brews?id=N — full brew report including series
// ============================================================================

void handleGetBrew(AsyncWebServerRequest* request) {
    if (!request->hasParam("id")) {
        web_portal_send_json_error(request, 400, "Missing id parameter");
        return;
    }

    unsigned id = 0;
    const String& id_str = request->getParam("id")->value();
    if (sscanf(id_str.c_str(), "%u", &id) != 1) {
        web_portal_send_json_error(request, 400, "Invalid id");
        return;
    }

    char path[32];
    snprintf(path, sizeof(path), "%s/%04u.json", BREW_LOG_DIR, id);

    if (!Storage.exists(path)) {
        web_portal_send_json_error(request, 404, "Brew not found");
        return;
    }

    File f = Storage.open(path, "r");
    if (!f) {
        web_portal_send_json_error(request, 500, "Failed to open brew file");
        return;
    }

    // Stream the raw JSON file with the id injected
    size_t sz = f.size();
    AsyncResponseStream* response = request->beginResponseStream("application/json");
    web_portal_add_cors_headers(response);

    // Inject id at the top: {"id":N, ...rest of file without leading '{' }
    response->printf("{\"id\":%u,", id);

    // Skip the opening '{' of the file and stream the rest
    f.read();  // consume '{'
    while (f.available()) {
        uint8_t buf[256];
        size_t read = f.readBytes((char*)buf, sizeof(buf));
        response->write(buf, read);
    }
    f.close();

    request->send(response);
}

// ============================================================================
// DELETE /api/brews?id=N — delete a specific brew
// DELETE /api/brews (no id) — clear all
// ============================================================================

void handleDeleteBrew(AsyncWebServerRequest* request) {
    if (!request->hasParam("id")) {
        // No id param — treat as delete all
        handleDeleteAllBrews(request);
        return;
    }

    unsigned id = 0;
    const String& id_str = request->getParam("id")->value();
    if (sscanf(id_str.c_str(), "%u", &id) != 1) {
        web_portal_send_json_error(request, 400, "Invalid id");
        return;
    }

    char path[32];
    snprintf(path, sizeof(path), "%s/%04u.json", BREW_LOG_DIR, id);

    if (!Storage.exists(path)) {
        web_portal_send_json_error(request, 404, "Brew not found");
        return;
    }

    Storage.remove(path);
    storage_publish_usage();

    LOGI(TAG, "Deleted brew %u", id);

    AsyncWebServerResponse* resp = request->beginResponse(200, "application/json", "{\"ok\":true}");
    web_portal_add_cors_headers(resp);
    request->send(resp);
}

// ============================================================================
// DELETE /api/brews — clear all brew history
// ============================================================================

void handleDeleteAllBrews(AsyncWebServerRequest* request) {
    File dir = Storage.open(BREW_LOG_DIR);
    uint16_t removed = 0;

    if (dir && dir.isDirectory()) {
        // Collect filenames first (can't delete while iterating on some FS impls).
        // Heap-allocate the name table rather than putting ~3.2 KB on the
        // AsyncTCP handler stack.
        char (*names)[16] = (char (*)[16])malloc(BREW_LOG_MAX_BREWS * sizeof(names[0]));
        if (!names) {
            web_portal_send_json_error(request, 503, "Out of memory");
            return;
        }
        uint16_t count = 0;

        File f = dir.openNextFile();
        while (f && count < BREW_LOG_MAX_BREWS) {
            if (!f.isDirectory()) {
                strlcpy(names[count], f.name(), sizeof(names[0]));
                count++;
            }
            f = dir.openNextFile();
        }

        for (uint16_t i = 0; i < count; i++) {
            char path[32];
            snprintf(path, sizeof(path), "%s/%s", BREW_LOG_DIR, names[i]);
            if (Storage.remove(path)) removed++;
        }
        free(names);
    }

    storage_publish_usage();

    LOGI(TAG, "Cleared all brews: %u removed", (unsigned)removed);

    AsyncWebServerResponse* resp = request->beginResponse(200, "application/json", "{\"ok\":true}");
    web_portal_add_cors_headers(resp);
    request->send(resp);
}

// ============================================================================
// POST /api/brews/import — import brew(s) from JSON
// ============================================================================

void handlePostBrewImport(AsyncWebServerRequest* request, uint8_t* data,
                          size_t len, size_t index, size_t total) {
    // Limit import size (200 brews × ~5KB = ~1MB max)
    const size_t MAX_IMPORT_SIZE = 1024 * 1024;

    if (index == 0) {
        if (total > MAX_IMPORT_SIZE) {
            web_portal_send_json_error(request, 413, "Import too large (max 1MB)");
            return;
        }
        // Accumulate the upload (~1 MB max) in PSRAM, not internal RAM, so a
        // large import can't OOM the heap on the P4 boards.
        uint8_t* buf = (uint8_t*)heap_caps_malloc(total + 1,
                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!buf) {
            web_portal_send_json_error(request, 503, "Out of memory");
            return;
        }
        request->_tempObject = buf;
    }

    uint8_t* buf = (uint8_t*)request->_tempObject;
    if (!buf) return;  // allocation failed on first chunk
    memcpy(buf + index, data, len);

    // Last chunk?
    if (index + len >= total) {
        buf[total] = '\0';
        request->_tempObject = nullptr;  // we own the pointer now

        // Parse — could be single object or array. Build the JsonDocument in
        // PSRAM via the shared helper so a large import doesn't exhaust the
        // internal heap.
        auto doc = make_psram_json_doc(total + 256);
        if (!doc) {
            heap_caps_free(buf);
            web_portal_send_json_error(request, 503, "Out of memory");
            return;
        }
        DeserializationError err = deserializeJson(*doc, (const char*)buf, total);
        heap_caps_free(buf);

        if (err) {
            web_portal_send_json_error(request, 400, "Invalid JSON");
            return;
        }

        uint16_t imported = 0;

        auto importBrew = [&](JsonObject brew) {
            if (!brew.containsKey("v") || !brew.containsKey("fields") || !brew.containsKey("series"))
                return;

            // Serialize back to string for brew_log_import_raw()
            size_t len = measureJson(brew);
            char* buf = (char*)malloc(len + 1);
            if (!buf) return;
            serializeJson(brew, buf, len + 1);

            if (brew_log_import_raw(buf, len)) imported++;
            free(buf);
        };

        if (doc->is<JsonArray>()) {
            for (JsonObject brew : doc->as<JsonArray>()) {
                importBrew(brew);
            }
        } else if (doc->is<JsonObject>()) {
            importBrew(doc->as<JsonObject>());
        }

        storage_publish_usage();

        LOGI(TAG, "Imported %u brews", (unsigned)imported);

        char resp_buf[64];
        snprintf(resp_buf, sizeof(resp_buf), "{\"ok\":true,\"imported\":%u}", (unsigned)imported);
        AsyncWebServerResponse* resp = request->beginResponse(200, "application/json", resp_buf);
        web_portal_add_cors_headers(resp);
        request->send(resp);
    }
}

void web_portal_brews_register_routes(AsyncWebServer* server) {
    server->on("/api/brews/import", HTTP_OPTIONS, [](AsyncWebServerRequest *r){ web_portal_send_cors_preflight(r); });
    server->on(
        "/api/brews/import",
        HTTP_POST,
        [](AsyncWebServerRequest *request) {
            if (!portal_auth_gate(request)) return;
        },
        NULL,
        handlePostBrewImport
    );

    server->on("/api/brews", HTTP_OPTIONS, [](AsyncWebServerRequest *r){ web_portal_send_cors_preflight(r); });
    server->on("/api/brews", HTTP_GET, handleGetBrews);
    server->on("/api/brews", HTTP_DELETE, [](AsyncWebServerRequest *request) {
        if (!portal_auth_gate(request)) return;
        handleDeleteBrew(request);
    });
}

REGISTER_ROUTES(web_portal_brews_register_routes);

#endif // IS_COFFEE_SCALE

#undef TAG
