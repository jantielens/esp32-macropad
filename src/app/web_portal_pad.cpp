#include "web_portal_pad.h"

#if HAS_DISPLAY

#include "board_config.h"
#include "icon_store.h"
#include "log_manager.h"
#include "pad_block.h"
#include "pad_config.h"
#include "pad_resolve_request.h"
#include "pad_validate.h"
#include "psram_json_allocator.h"
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

// Validate a pad JSON body before saving. Delegates to the shared pad_validate
// (single source of truth, identical to the MCP write/validate tools) so the
// portal rejects the same invalid pads MCP does — binding tokens, widget
// fields, colors, action arrays, length caps, and the one-level [pad:] rule.
// tolerate_offgrid=true preserves the portal's feature of keeping buttons that
// fall outside a shrunken grid (they are hidden and reappear when it grows).
// Returns error message or nullptr on success.
static const char* validate_pad_json(const uint8_t* json, size_t len) {
    BasicJsonDocument<PsramJsonAllocator> doc(len * 2 + 512);
    DeserializationError err = deserializeJson(doc, json, len);
    if (err) return "Invalid JSON";
    return pad_validate(doc.as<JsonObjectConst>(), /*tolerate_offgrid=*/true);
}

static uint8_t* allocate_pad_json_buffer(size_t len) {
    uint8_t* buffer = nullptr;
    if (psramFound()) {
        buffer = (uint8_t*)heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!buffer) {
        buffer = (uint8_t*)heap_caps_malloc(len + 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return buffer;
}

static void remove_pad_image_passwords(JsonArray buttons) {
    for (JsonObject button : buttons) {
        const char* password = button["bg_image_password"] | "";
        const bool password_set = password[0] != '\0';
        button.remove("bg_image_password");
        button["bg_image_password_set"] = password_set;
    }
}

static uint8_t* prepare_pad_save_json(uint8_t page, const uint8_t* json, size_t len, size_t* prepared_len) {
    if (!prepared_len) return nullptr;

    BasicJsonDocument<PsramJsonAllocator> submitted(len * 2 + 512);
    if (deserializeJson(submitted, json, len)) return nullptr;

    size_t existing_len = 0;
    char* existing_json = pad_config_read_raw(page, &existing_len);
    BasicJsonDocument<PsramJsonAllocator> existing(existing_len * 2 + 512);
    const bool have_existing = existing_json && !deserializeJson(existing, existing_json, existing_len);

    JsonArray submitted_buttons = submitted["buttons"].as<JsonArray>();
    JsonArrayConst existing_buttons = existing["buttons"].as<JsonArrayConst>();
    for (JsonObject button : submitted_buttons) {
        const char* submitted_password = button["bg_image_password"] | "";
        const bool preserve_password = (button["bg_image_password_set"] | false) && submitted_password[0] == '\0';
        button.remove("bg_image_password_set");
        if (!preserve_password || !have_existing) continue;

        const int col = button["col"] | -1;
        const int row = button["row"] | -1;
        for (JsonObjectConst existing_button : existing_buttons) {
            if ((existing_button["col"] | -2) != col || (existing_button["row"] | -2) != row) continue;
            const char* password = existing_button["bg_image_password"] | "";
            if (password[0] != '\0') button["bg_image_password"] = password;
            break;
        }
    }

    if (existing_json) free(existing_json);

    const size_t serialized_len = measureJson(submitted);
    uint8_t* prepared = allocate_pad_json_buffer(serialized_len);
    if (!prepared) return nullptr;

    serializeJson(submitted, prepared, serialized_len + 1);
    *prepared_len = serialized_len;
    return prepared;
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

    std::shared_ptr<BasicJsonDocument<PsramJsonAllocator>> doc = make_psram_json_doc(len * 2 + 512);
    if (!doc || deserializeJson(*doc, json, len)) {
        free(json);
        web_portal_send_json_error(request, 500, "Failed to read config");
        return;
    }
    free(json);

    remove_pad_image_passwords((*doc)["buttons"].as<JsonArray>());
    web_portal_send_json_chunked(request, doc);
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

    size_t prepared_len = 0;
    uint8_t* prepared_json = prepare_pad_save_json(g_pad_post.page, g_pad_post.buf, g_pad_post.total, &prepared_len);
    if (!prepared_json) {
        web_portal_send_json_error(request, 400, "Invalid JSON");
        pad_post_reset();
        return;
    }

    const char* err = validate_pad_json(prepared_json, prepared_len);
    if (err) {
        LOGW(TAG, "Validation failed for page %u: %s", g_pad_post.page, err);
        web_portal_send_json_error(request, 400, err);
        heap_caps_free(prepared_json);
        pad_post_reset();
        return;
    }

    if (!pad_config_save_raw(g_pad_post.page, prepared_json, prepared_len)) {
        web_portal_send_json_error(request, 500, "Failed to save config");
        heap_caps_free(prepared_json);
        pad_post_reset();
        return;
    }
    heap_caps_free(prepared_json);

    uint8_t saved_page = g_pad_post.page;
    pad_post_reset();

    LOGI(TAG, "Page %u config saved", saved_page);
    // Icons are already cached by icon_store_install() from the individual
    // icon uploads that the browser performs before POSTing the config.
#if HAS_DISPLAY
    // Refresh the built-in 'pads' list provider so newly added/renamed pads
    // appear in list widgets without a reboot.
    extern void list_provider_pads_invalidate();
    list_provider_pads_invalidate();
#endif
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

#if HAS_DISPLAY
    extern void list_provider_pads_invalidate();
    list_provider_pads_invalidate();
#endif
#if HAS_MQTT
    mqtt_sub_store_subscribe_all();
#endif
    request->send(200, "application/json", "{\"success\":true}");
}

// ============================================================================
// GET /api/pad/blocks — building block catalog
// ============================================================================
void handleGetPadBlocks(AsyncWebServerRequest *request) {
    if (!portal_auth_gate(request)) return;

    const uint8_t count = pad_block_catalog_count();
    const PadBlock* const* catalog = pad_block_catalog();

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    response->print('[');
    for (uint8_t i = 0; i < count; i++) {
        const PadBlock* blk = catalog[i];
        if (i > 0) response->print(',');
        response->print("{\"id\":\"");
        response->print(blk->id);
        response->print("\",\"name\":\"");
        response->print(blk->name);
        response->print("\",\"desc\":\"");
        response->print(blk->desc);
        response->print("\",\"icon\":\"");
        response->print(blk->icon);
        response->printf("\",\"min_cols\":%u,\"min_rows\":%u,\"min_free\":%u,\"buttons\":[",
                         blk->min_cols, blk->min_rows, blk->min_free_cells);
        for (uint8_t j = 0; j < blk->button_count; j++) {
            const PadBlockButton& btn = blk->buttons[j];
            if (j > 0) response->print(',');
            // Inject per-button JSON with positional fields prepended
            response->printf("{\"col_offset\":%u,\"row_offset\":%u,\"col_span\":%u,\"row_span\":%u,",
                             btn.col_offset, btn.row_offset, btn.col_span, btn.row_span);
            // btn.json is a JSON object string starting with '{' — skip the opening brace
            // to merge its fields into the object we already opened
            if (btn.json && btn.json[0] == '{') {
                response->print(btn.json + 1); // includes the closing '}'
            } else {
                response->print('}');
            }
        }
        response->print("],\"bindings\":{");
        for (uint8_t j = 0; j < blk->binding_count; j++) {
            if (j > 0) response->print(',');
            response->print('"');
            response->print(blk->bindings[j].name);
            response->print("\":\"");
            response->print(blk->bindings[j].value);
            response->print('"');
        }
        response->print("}}");
    }
    response->print(']');
    request->send(response);
}

#if HAS_MQTT
// ============================================================================
// POST /api/pad/resolve — resolve binding tokens against LIVE data (no save).
// Body: { screen?, bindings?[], button? }. Powers the pad editor live preview.
// Delegates to the shared pad_resolve_request() (same engine as the MCP
// resolve_bindings tool); resolution runs on the main loop via the bridge.
// ============================================================================
#define PAD_RESOLVE_MAX_JSON_BYTES (8 * 1024)

static struct {
    bool     in_progress;
    uint32_t started_ms;
    size_t   total;
    size_t   received;
    uint8_t* buf;
} g_resolve_post = {false, 0, 0, 0, nullptr};

static void resolve_post_reset() {
    if (g_resolve_post.buf) { heap_caps_free(g_resolve_post.buf); g_resolve_post.buf = nullptr; }
    g_resolve_post.in_progress = false;
    g_resolve_post.total = 0;
    g_resolve_post.received = 0;
    g_resolve_post.started_ms = 0;
}

void handlePostPadResolve(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (!portal_auth_gate(request)) return;

    if (index == 0) {
        const uint32_t now = millis();
        if (g_resolve_post.in_progress && g_resolve_post.started_ms &&
            (now - g_resolve_post.started_ms > 10000)) {
            resolve_post_reset();
        }
        if (g_resolve_post.in_progress) {
            web_portal_send_json_error(request, 409, "Resolve already in progress");
            return;
        }
        if (total == 0 || total > PAD_RESOLVE_MAX_JSON_BYTES) {
            web_portal_send_json_error(request, 413, "JSON body too large");
            return;
        }
        uint8_t* buf = (uint8_t*)heap_caps_malloc(total + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!buf) buf = (uint8_t*)heap_caps_malloc(total + 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!buf) { web_portal_send_json_error(request, 503, "Out of memory"); return; }
        g_resolve_post.in_progress = true;
        g_resolve_post.started_ms = now;
        g_resolve_post.total = total;
        g_resolve_post.received = 0;
        g_resolve_post.buf = buf;
    }

    if (!g_resolve_post.in_progress || !g_resolve_post.buf ||
        g_resolve_post.total != total || (index + len) > total) {
        web_portal_send_json_error(request, 400, "Invalid upload state");
        resolve_post_reset();
        return;
    }

    memcpy(g_resolve_post.buf + index, data, len);
    size_t new_received = index + len;
    if (new_received > g_resolve_post.received) g_resolve_post.received = new_received;
    if (g_resolve_post.received < g_resolve_post.total) return;  // more chunks

    g_resolve_post.buf[g_resolve_post.total] = '\0';

    BasicJsonDocument<PsramJsonAllocator> doc(g_resolve_post.total * 2 + 1024);
    DeserializationError perr = deserializeJson(doc, g_resolve_post.buf, g_resolve_post.total);
    resolve_post_reset();
    if (perr) { web_portal_send_json_error(request, 400, "Invalid JSON"); return; }

    auto out = make_psram_json_doc(8 * 1024);
    if (!out) { web_portal_send_json_error(request, 503, "Out of memory"); return; }
    JsonObject result = out->to<JsonObject>();

    const char* em = nullptr;
    PadResolveStatus st = pad_resolve_request(doc.as<JsonObjectConst>(), result, &em);
    if (st != PAD_RESOLVE_OK) {
        int code = (st == PAD_RESOLVE_BAD_PARAMS) ? 400
                 : (st == PAD_RESOLVE_BUSY)       ? 503
                 : (st == PAD_RESOLVE_OOM)        ? 503 : 500;
        web_portal_send_json_error(request, code, em ? em : "resolve failed");
        return;
    }

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    serializeJson(result, *response);
    request->send(response);
}
#endif // HAS_MQTT

#endif // HAS_DISPLAY
