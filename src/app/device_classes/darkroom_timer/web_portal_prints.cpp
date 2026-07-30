// Print session log REST API — /api/prints (query-param resource shape).
//
//   GET    /api/prints              — list all prints (newest first, fields only)
//   GET    /api/prints?id=ID        — full print detail (segments, notes, star)
//   PUT    /api/prints?id=ID        — update mutable fields (notes, starred)
//   DELETE /api/prints?id=ID        — delete a single print
//   DELETE /api/prints?confirm=true — delete all prints
//   GET    /api/prints/export       — export all prints as a JSON array
//
// Self-registers its AsyncWebServer routes via REGISTER_ROUTES() (see
// web_portal_routes.h), mirroring web_portal_relay.cpp. The static initializer
// runs because this file is #include-aggregated into route_components.cpp under
// #if IS_DARKROOM_TIMER.
//
// Persistence goes through the Storage facade (LittleFS or SD). Flash I/O is
// wrapped in a panel-sleep blank/restore window because DSI panels DMA-scan
// PSRAM continuously and flash writes starve the display DMA, causing flicker
// (same mitigation print_log.cpp uses for deferred writes).

#include "board_config.h"

#if IS_DARKROOM_TIMER

#include "display_manager.h"
#include "log_manager.h"
#include "print_log.h"
#include "storage.h"
#include "web_portal_auth.h"
#include "web_portal_cors.h"
#include "web_portal_json.h"
#include "web_portal_routes.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <esp_task_wdt.h>

#define TAG "PrintAPI"

// ============================================================================
// Display blanking — halt DSI DMA around flash I/O to prevent bus contention.
// Mirrors the helper in print_log.cpp (panel sleep + backlight off).
// ============================================================================

static uint8_t blank_display() {
#if HAS_DISPLAY
    uint8_t saved = display_manager_get_backlight_brightness();
    if (saved > 0) display_manager_set_backlight_brightness(0);
    if (displayManager && displayManager->getDriver()) {
        displayManager->lock();
        displayManager->getDriver()->displaySleep();
        displayManager->unlock();
    }
    return saved;
#else
    return 0;
#endif
}

static void restore_display(uint8_t saved) {
#if HAS_DISPLAY
    if (displayManager && displayManager->getDriver()) {
        displayManager->lock();
        displayManager->getDriver()->displayWake();
        // displaySleep() zeroes the DPI framebuffer, but LVGL's dirty-area
        // tracking still believes the screen is painted, so it would only
        // redraw self-invalidating widgets — leaving the rest black until the
        // next navigation. Force a full-screen invalidate so the entire UI
        // repaints into the freshly-blanked framebuffers.
        lv_obj_t* scr = lv_screen_active();
        if (scr) lv_obj_invalidate(scr);
        displayManager->unlock();
    }
    if (saved > 0) display_manager_set_backlight_brightness(saved);
#else
    (void)saved;
#endif
}

// ============================================================================
// Helpers
// ============================================================================

// Collect print IDs (filename minus .json), sorted newest-first (lexicographic
// descending — valid because IDs are YYMMDD-NNN). Returns the number found.
static uint16_t collect_print_ids(char ids[][20], uint16_t max_ids) {
    File dir = Storage.open(PRINT_LOG_DIR);
    if (!dir || !dir.isDirectory()) return 0;

    uint16_t count = 0;
    File f = dir.openNextFile();
    while (f && count < max_ids) {
        if (!f.isDirectory()) {
            const char* name = f.name();
            size_t nlen = strlen(name);
            if (nlen > 5 && strcmp(name + nlen - 5, ".json") == 0) {
                size_t id_len = nlen - 5;
                if (id_len < 20) {
                    memcpy(ids[count], name, id_len);
                    ids[count][id_len] = '\0';
                    count++;
                }
            }
        }
        f = dir.openNextFile();
    }

    // Insertion-free selection sort, descending — small N (<=500), runs once.
    for (uint16_t i = 0; i < count; i++) {
        for (uint16_t j = i + 1; j < count; j++) {
            if (strcmp(ids[j], ids[i]) > 0) {
                char tmp[20];
                memcpy(tmp, ids[i], 20);
                memcpy(ids[i], ids[j], 20);
                memcpy(ids[j], tmp, 20);
            }
        }
    }

    return count;
}

// ============================================================================
// GET /api/prints?id=ID — full print detail (raw file stream)
// ============================================================================

static void handleGetPrint(AsyncWebServerRequest* request) {
    const String& id_str = request->getParam("id")->value();
    if (id_str.length() == 0 || id_str.length() > 18) {
        web_portal_send_json_error(request, 400, "Invalid id");
        return;
    }

    char path[48];
    snprintf(path, sizeof(path), "%s/%s.json", PRINT_LOG_DIR, id_str.c_str());

    if (!Storage.exists(path)) {
        web_portal_send_json_error(request, 404, "Print not found");
        return;
    }

    File f = Storage.open(path, "r");
    if (!f) {
        web_portal_send_json_error(request, 500, "Failed to open print file");
        return;
    }

    AsyncResponseStream* response = request->beginResponseStream("application/json");
    web_portal_add_cors_headers(response);
    while (f.available()) {
        uint8_t buf[256];
        size_t read = f.readBytes((char*)buf, sizeof(buf));
        response->write(buf, read);
    }
    f.close();
    request->send(response);
}

// ============================================================================
// GET /api/prints — list all prints (fields + notes + starred, no segments)
// ============================================================================

static void handleGetPrints(AsyncWebServerRequest* request) {
    if (!portal_auth_gate(request)) return;

    if (request->hasParam("id")) {
        handleGetPrint(request);
        return;
    }

    static constexpr uint16_t MAX_IDS = DARKROOM_PRINT_LOG_MAX;
    char (*ids)[20] = (char(*)[20])malloc(MAX_IDS * 20);
    if (!ids) {
        web_portal_send_json_error(request, 503, "Out of memory");
        return;
    }

    uint16_t count = collect_print_ids(ids, MAX_IDS);

    AsyncResponseStream* response = request->beginResponseStream("application/json");
    web_portal_add_cors_headers(response);
    response->print("{\"prints\":[");

    for (uint16_t i = 0; i < count; i++) {
        // Feed WDT — each iteration does flash I/O + JSON parse which can
        // exceed the 5s async_tcp watchdog when many prints exist.
        if ((i & 7) == 0) esp_task_wdt_reset();

        char path[48];
        snprintf(path, sizeof(path), "%s/%s.json", PRINT_LOG_DIR, ids[i]);

        File f = Storage.open(path, "r");
        if (!f) continue;

        size_t sz = f.size();
        if (sz > 4096) { f.close(); continue; }

        char* buf = (char*)malloc(sz + 1);
        if (!buf) { f.close(); continue; }
        f.readBytes(buf, sz);
        buf[sz] = '\0';
        f.close();

        StaticJsonDocument<3072> doc;
        DeserializationError err = deserializeJson(doc, buf, sz);
        free(buf);
        if (err) continue;

        if (i > 0) response->print(',');
        response->print("{\"v\":");
        response->print(doc["v"] | 1);
        response->print(",\"fields\":");
        serializeJson(doc["fields"], *response);
        if (doc.containsKey("notes")) {
            const char* notes = doc["notes"] | "";
            if (notes[0]) {
                response->print(",\"notes\":");
                serializeJson(doc["notes"], *response);
            }
        }
        if (doc["starred"] | false) {
            response->print(",\"starred\":true");
        }
        response->print('}');
    }

    response->printf("],\"count\":%u,\"max\":%u}", (unsigned)count,
                     (unsigned)DARKROOM_PRINT_LOG_MAX);
    free(ids);
    request->send(response);
}

// ============================================================================
// PUT /api/prints?id=ID — update mutable fields (notes, starred)
// ============================================================================

#define PRINT_PUT_MAX_BYTES 2048

struct PrintPutState {
    bool    errored;
    size_t  total;
    size_t  received;
    uint8_t buf[PRINT_PUT_MAX_BYTES + 1];
};

static void handlePutPrintBody(AsyncWebServerRequest* request, uint8_t* data,
                               size_t len, size_t index, size_t total) {
    if (index == 0) {
        if (!portal_auth_gate(request)) return;

        auto* state = new PrintPutState{};
        state->errored  = false;
        state->total    = total;
        state->received = 0;
        request->_tempObject = state;

        if (total == 0 || total > PRINT_PUT_MAX_BYTES) {
            state->errored = true;
            web_portal_send_json_error(request, 413, "Body too large");
            return;
        }
    }

    auto* state = static_cast<PrintPutState*>(request->_tempObject);
    if (!state || state->errored) return;

    if (state->received + len > PRINT_PUT_MAX_BYTES) {
        state->errored = true;
        web_portal_send_json_error(request, 413, "Body too large");
        return;
    }
    memcpy(state->buf + state->received, data, len);
    state->received += len;

    if (state->received < state->total) return;  // more chunks to come
    state->buf[state->received] = '\0';

    StaticJsonDocument<256> update_doc;
    DeserializationError err =
        deserializeJson(update_doc, (const char*)state->buf, state->received);
    if (err) {
        web_portal_send_json_error(request, 400, "Invalid JSON");
        return;
    }

    if (!request->hasParam("id")) {
        web_portal_send_json_error(request, 400, "Missing id parameter");
        return;
    }
    const String& id_str = request->getParam("id")->value();
    if (id_str.length() == 0 || id_str.length() > 18) {
        web_portal_send_json_error(request, 400, "Invalid id");
        return;
    }

    char path[48];
    snprintf(path, sizeof(path), "%s/%s.json", PRINT_LOG_DIR, id_str.c_str());

    if (!Storage.exists(path)) {
        web_portal_send_json_error(request, 404, "Print not found");
        return;
    }

    File f = Storage.open(path, "r");
    if (!f) {
        web_portal_send_json_error(request, 500, "Failed to read print");
        return;
    }
    size_t sz = f.size();
    if (sz > 4096) {
        f.close();
        web_portal_send_json_error(request, 500, "Print file too large");
        return;
    }
    char* file_buf = (char*)malloc(sz + 1);
    if (!file_buf) {
        f.close();
        web_portal_send_json_error(request, 503, "Out of memory");
        return;
    }
    f.readBytes(file_buf, sz);
    file_buf[sz] = '\0';
    f.close();

    DynamicJsonDocument doc(sz + 512);
    err = deserializeJson(doc, file_buf, sz);
    free(file_buf);
    if (err) {
        web_portal_send_json_error(request, 500, "Corrupt print JSON");
        return;
    }

    if (update_doc.containsKey("notes")) {
        const char* notes = update_doc["notes"] | "";
        if (notes[0]) {
            doc["notes"] = notes;
        } else {
            doc.remove("notes");
        }
    }
    if (update_doc.containsKey("starred")) {
        if (update_doc["starred"] | false) {
            doc["starred"] = true;
        } else {
            doc.remove("starred");
        }
    }

    // Halt DSI DMA around the flash write to prevent contention flicker.
    uint8_t saved_bl = blank_display();
    f = Storage.open(path, "w");
    if (!f) {
        restore_display(saved_bl);
        web_portal_send_json_error(request, 500, "Failed to write print");
        return;
    }
    serializeJson(doc, f);
    f.close();
    restore_display(saved_bl);

    LOGI(TAG, "Updated print %s", id_str.c_str());

    AsyncWebServerResponse* resp =
        request->beginResponse(200, "application/json", "{\"ok\":true}");
    web_portal_add_cors_headers(resp);
    request->send(resp);
}

// ============================================================================
// DELETE /api/prints?id=ID        — delete single
// DELETE /api/prints?confirm=true — delete all
// ============================================================================

static void handleDeletePrints(AsyncWebServerRequest* request) {
    if (!portal_auth_gate(request)) return;

    if (request->hasParam("id")) {
        const String& id_str = request->getParam("id")->value();
        if (id_str.length() == 0 || id_str.length() > 18) {
            web_portal_send_json_error(request, 400, "Invalid id");
            return;
        }

        char path[48];
        snprintf(path, sizeof(path), "%s/%s.json", PRINT_LOG_DIR, id_str.c_str());

        if (!Storage.exists(path)) {
            web_portal_send_json_error(request, 404, "Print not found");
            return;
        }

        uint8_t saved_bl = blank_display();
        Storage.remove(path);
        restore_display(saved_bl);

        LOGI(TAG, "Deleted print %s", id_str.c_str());

        AsyncWebServerResponse* resp =
            request->beginResponse(200, "application/json", "{\"ok\":true}");
        web_portal_add_cors_headers(resp);
        request->send(resp);
        return;
    }

    if (!request->hasParam("confirm") ||
        request->getParam("confirm")->value() != "true") {
        web_portal_send_json_error(request, 400, "Missing confirm=true parameter");
        return;
    }

    File dir = Storage.open(PRINT_LOG_DIR);
    uint16_t removed = 0;
    if (dir && dir.isDirectory()) {
        // Collect names first — can't delete while iterating the directory.
        char (*names)[32] = (char(*)[32])malloc(DARKROOM_PRINT_LOG_MAX * 32);
        if (!names) {
            web_portal_send_json_error(request, 503, "Out of memory");
            return;
        }
        uint16_t count = 0;
        File f = dir.openNextFile();
        while (f && count < DARKROOM_PRINT_LOG_MAX) {
            if (!f.isDirectory()) {
                strlcpy(names[count], f.name(), 32);
                count++;
            }
            f = dir.openNextFile();
        }

        uint8_t saved_bl = blank_display();
        for (uint16_t i = 0; i < count; i++) {
            if ((i & 7) == 0) esp_task_wdt_reset();
            char path[48];
            snprintf(path, sizeof(path), "%s/%s", PRINT_LOG_DIR, names[i]);
            if (Storage.remove(path)) removed++;
        }
        restore_display(saved_bl);
        free(names);
    }

    LOGI(TAG, "Cleared all prints: %u removed", (unsigned)removed);

    AsyncWebServerResponse* resp =
        request->beginResponse(200, "application/json", "{\"ok\":true}");
    web_portal_add_cors_headers(resp);
    request->send(resp);
}

// ============================================================================
// GET /api/prints/export — all prints as a JSON array (full data, download)
// ============================================================================

static void handleGetPrintsExport(AsyncWebServerRequest* request) {
    if (!portal_auth_gate(request)) return;

    static constexpr uint16_t MAX_IDS = DARKROOM_PRINT_LOG_MAX;
    char (*ids)[20] = (char(*)[20])malloc(MAX_IDS * 20);
    if (!ids) {
        web_portal_send_json_error(request, 503, "Out of memory");
        return;
    }

    uint16_t count = collect_print_ids(ids, MAX_IDS);

    AsyncResponseStream* response = request->beginResponseStream("application/json");
    web_portal_add_cors_headers(response);
    response->addHeader("Content-Disposition",
                        "attachment; filename=\"prints.json\"");
    response->print('[');

    bool first = true;
    for (uint16_t i = 0; i < count; i++) {
        if ((i & 7) == 0) esp_task_wdt_reset();

        char path[48];
        snprintf(path, sizeof(path), "%s/%s.json", PRINT_LOG_DIR, ids[i]);

        File f = Storage.open(path, "r");
        if (!f) continue;

        if (!first) response->print(',');
        first = false;

        while (f.available()) {
            uint8_t buf[256];
            size_t read = f.readBytes((char*)buf, sizeof(buf));
            response->write(buf, read);
        }
        f.close();
    }

    response->print(']');
    free(ids);
    request->send(response);
}

// ============================================================================
// Route registration
// ============================================================================

static void prints_register(AsyncWebServer* server) {
    server->on("/api/prints/export", HTTP_GET, handleGetPrintsExport);
    server->on("/api/prints", HTTP_GET, handleGetPrints);
    server->on("/api/prints", HTTP_DELETE, handleDeletePrints);
    server->on("/api/prints", HTTP_PUT,
               // Request handler fires after all body chunks — frees per-request state.
               [](AsyncWebServerRequest* request) {
                   if (request->_tempObject) {
                       delete static_cast<PrintPutState*>(request->_tempObject);
                       request->_tempObject = nullptr;
                   }
               },
               nullptr, handlePutPrintBody);
    LOGI(TAG, "Registered print log routes");
}

REGISTER_ROUTES(prints_register)

#endif // IS_DARKROOM_TIMER
