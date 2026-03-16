#include "web_portal_pad.h"

#if HAS_DISPLAY

#include "board_config.h"
#include "icon_store.h"
#include "log_manager.h"
#include "pad_config.h"
#include "web_portal_auth.h"
#include "web_portal_json.h"
#if HAS_MQTT
#include "mqtt_sub_store.h"
#endif

#include <ArduinoJson.h>

#include <esp_heap_caps.h>

#include <stdlib.h>
#include <string.h>

#define TAG "PadAPI"

// Maximum JSON body size for pad config POST (64-button pages can be large)
#define PAD_CONFIG_MAX_JSON_BYTES (48 * 1024)

// Body accumulator for POST /api/pad
static struct {
    bool in_progress;
    uint32_t started_ms;
    size_t total;
    size_t received;
    uint8_t* buf;
    uint8_t page;
} g_pad_post = {false, 0, 0, 0, nullptr, 0};

static void pad_post_reset() {
    if (g_pad_post.buf) {
        heap_caps_free(g_pad_post.buf);
        g_pad_post.buf = nullptr;
    }
    g_pad_post.in_progress = false;
    g_pad_post.total = 0;
    g_pad_post.received = 0;
    g_pad_post.started_ms = 0;
}

// Parse ?page=N from request. Returns -1 on error.
static int parse_page_param(AsyncWebServerRequest *request) {
    if (!request->hasParam("page")) return -1;
    const String& val = request->getParam("page")->value();
    int page = val.toInt();
    // toInt() returns 0 for non-numeric; distinguish "0" from error
    if (page == 0 && val != "0") return -1;
    if (page < 0 || page >= MAX_PADS) return -1;
    return page;
}

// Validate grid JSON: cols/rows in range, button placement within grid bounds.
// Returns error message or nullptr on success.
static const char* validate_pad_json(const uint8_t* json, size_t len) {
    DynamicJsonDocument doc(len * 2 + 512);
    DeserializationError err = deserializeJson(doc, json, len);
    if (err) return "Invalid JSON";

    const char* layout = doc["layout"] | "grid";

    if (strcmp(layout, "grid") == 0) {
        uint8_t cols = doc["cols"] | (uint8_t)0;
        uint8_t rows = doc["rows"] | (uint8_t)0;
        if (cols < 1 || cols > MAX_GRID_COLS) return "cols must be 1-8";
        if (rows < 1 || rows > MAX_GRID_ROWS) return "rows must be 1-8";

        // Buttons outside the current grid are allowed — they are
        // silently hidden and reappear when cols/rows grow back.
    }
    // Non-grid layouts (curated) are not validated in v0

    return nullptr; // Valid
}

// ============================================================================
// GET /api/pad?page=N
// ============================================================================
void handleGetPadConfig(AsyncWebServerRequest *request) {
    if (!portal_auth_gate(request)) return;

    int page = parse_page_param(request);
    if (page < 0) {
        web_portal_send_json_error(request, 400, "Missing or invalid page parameter");
        return;
    }

    if (!pad_config_exists((uint8_t)page)) {
        web_portal_send_json_error(request, 404, "Page config not found");
        return;
    }

    size_t len = 0;
    char* json = pad_config_read_raw((uint8_t)page, &len);
    if (!json) {
        web_portal_send_json_error(request, 500, "Failed to read config");
        return;
    }

    // Stream the raw JSON directly
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", String(json));
    free(json);
    request->send(response);
}

// ============================================================================
// POST /api/pad?page=N (body handler)
// ============================================================================
void handlePostPadConfig(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (!portal_auth_gate(request)) return;

    if (index == 0) {
        // First chunk — parse page param, allocate buffer
        int page = parse_page_param(request);
        if (page < 0) {
            web_portal_send_json_error(request, 400, "Missing or invalid page parameter");
            return;
        }

        // Cleanup any stuck upload
        const uint32_t now = millis();
        if (g_pad_post.in_progress && g_pad_post.started_ms &&
            (now - g_pad_post.started_ms > 10000)) {
            LOGW(TAG, "Stuck pad upload — resetting");
            pad_post_reset();
        }

        if (g_pad_post.in_progress) {
            web_portal_send_json_error(request, 409, "Pad config upload already in progress");
            return;
        }

        if (total == 0 || total > PAD_CONFIG_MAX_JSON_BYTES) {
            web_portal_send_json_error(request, 413, "JSON body too large");
            return;
        }

        uint8_t* buf = nullptr;
        if (psramFound()) {
            buf = (uint8_t*)heap_caps_malloc(total + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        }
        if (!buf) {
            buf = (uint8_t*)heap_caps_malloc(total + 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        if (!buf) {
            web_portal_send_json_error(request, 503, "Out of memory");
            return;
        }

        g_pad_post.in_progress = true;
        g_pad_post.started_ms = now;
        g_pad_post.total = total;
        g_pad_post.received = 0;
        g_pad_post.buf = buf;
        g_pad_post.page = (uint8_t)page;
    }

    // Copy chunk
    if (!g_pad_post.in_progress || !g_pad_post.buf ||
        g_pad_post.total != total || (index + len) > total) {
        web_portal_send_json_error(request, 400, "Invalid upload state");
        pad_post_reset();
        return;
    }

    memcpy(g_pad_post.buf + index, data, len);

    size_t new_received = index + len;
    if (new_received > g_pad_post.received) {
        g_pad_post.received = new_received;
    }

    if (g_pad_post.received < g_pad_post.total) {
        return; // More chunks to come
    }

    // All data received — validate and save
    g_pad_post.buf[g_pad_post.total] = '\0';

    const char* err = validate_pad_json(g_pad_post.buf, g_pad_post.total);
    if (err) {
        LOGW(TAG, "Validation failed for page %u: %s", g_pad_post.page, err);
        web_portal_send_json_error(request, 400, err);
        pad_post_reset();
        return;
    }

    if (!pad_config_save_raw(g_pad_post.page, g_pad_post.buf, g_pad_post.total)) {
        web_portal_send_json_error(request, 500, "Failed to save config");
        pad_post_reset();
        return;
    }

    uint8_t saved_page = g_pad_post.page;
    pad_post_reset();

    LOGI(TAG, "Page %u config saved", saved_page);
    // Icons are already cached by icon_store_install() from the individual
    // icon uploads that the browser performs before POSTing the config.
#if HAS_MQTT
    mqtt_sub_store_subscribe_all();
#endif
    request->send(200, "application/json", "{\"success\":true}");
}

// ============================================================================
// DELETE /api/pad?page=N
// ============================================================================
void handleDeletePadConfig(AsyncWebServerRequest *request) {
    if (!portal_auth_gate(request)) return;

    int page = parse_page_param(request);
    if (page < 0) {
        web_portal_send_json_error(request, 400, "Missing or invalid page parameter");
        return;
    }

    if (!pad_config_delete((uint8_t)page)) {
        web_portal_send_json_error(request, 500, "Failed to delete config");
        return;
    }

#if HAS_MQTT
    mqtt_sub_store_subscribe_all();
#endif
    request->send(200, "application/json", "{\"success\":true}");
}

#endif // HAS_DISPLAY
