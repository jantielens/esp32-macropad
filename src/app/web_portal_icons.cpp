#include "web_portal_icons.h"

#if HAS_DISPLAY

#include "board_config.h"
#include "button_defaults.h"
#include "display_manager.h"
#include "icon_store.h"
#include "log_manager.h"
#include "ota_activity.h"
#include "pad_layout.h"
#include "web_portal_auth.h"
#include "web_portal_json.h"
#include "web_portal_utils.h"

#include "storage.h"
#include <esp_heap_caps.h>
#include <new>
#include <string.h>
#include <stdlib.h>

#define TAG "IconAPI"

struct IconUploadState {
    bool source_upload;
    bool png_source;
    size_t total;
    size_t received;
    uint8_t* data;
    IconSourceUpload* stream;
    char key[CONFIG_ICON_KEY_MAX_LEN];
    char source_id[CONFIG_ICON_SOURCE_ID_MAX_LEN];
    IconKind kind;
    uint32_t render_key;
};

static void icon_upload_cleanup(AsyncWebServerRequest* request) {
    auto* state = static_cast<IconUploadState*>(request->_tempObject);
    if (!state) return;
    request->_tempObject = nullptr;
    icon_store_source_png_abort(state->stream);
    if (state->data) heap_caps_free(state->data);
    delete state;
}

static void icon_store_send_error(AsyncWebServerRequest* request, IconStoreResult result) {
    switch (result) {
        case ICON_STORE_INVALID: web_portal_send_json_error(request, 400, "Invalid icon source or PNG"); return;
        case ICON_STORE_TOO_LARGE: web_portal_send_json_error(request, 413, "Icon data exceeds the supported size"); return;
        case ICON_STORE_BUSY:
        case ICON_STORE_CONFLICT: web_portal_send_json_error(request, 409, "Another icon storage operation is in progress"); return;
        case ICON_STORE_UNAVAILABLE: web_portal_send_json_error(request, 503, "Not enough memory to store icon data"); return;
        case ICON_STORE_IO_ERROR: web_portal_send_json_error(request, 507, "Icon storage operation failed"); return;
        case ICON_STORE_OK: return;
    }
}

static void handleIconUploadBody(AsyncWebServerRequest* request, uint8_t* data,
                                 size_t length, size_t index, size_t total,
                                 bool source_upload) {
    if (!portal_auth_gate(request)) {
        icon_upload_cleanup(request);
        return;
    }
    if (ota_activity_is_active()) {
        const bool owned = request->_tempObject != nullptr;
        icon_upload_cleanup(request);
        if (index == 0 || owned)
            web_portal_send_json_error(request, 409, "Firmware update is in progress");
        return;
    }
    if (index == 0 && !request->_tempObject) {
        const bool png_source = source_upload && request->contentType() == "image/png";
        if (source_upload && !png_source && request->contentType() != "application/json") {
            web_portal_send_json_error(request, 415, "Expected image/png or application/json");
            return;
        }
        const size_t maximum = source_upload && !png_source
            ? ICON_MAX_CATALOG_SOURCE_JSON_SIZE : ICON_MAX_PNG_SIZE;
        if (!total || total > maximum) {
            web_portal_send_json_error(request, total > maximum ? 413 : 400,
                                       "Icon data is empty or too large");
            return;
        }
        auto* state = new (std::nothrow) IconUploadState{};
        if (!state) {
            web_portal_send_json_error(request, 503, "Unable to start icon upload");
            return;
        }
        state->png_source = png_source;
        if (png_source) {
            const IconStoreResult result = icon_store_source_png_begin(total, &state->stream);
            if (result != ICON_STORE_OK) {
                delete state;
                icon_store_send_error(request, result);
                return;
            }
        } else {
            if (psramFound()) state->data = (uint8_t*)heap_caps_malloc(total + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!state->data) state->data = (uint8_t*)heap_caps_malloc(total + 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (!state->data) {
                delete state;
                web_portal_send_json_error(request, 503, "Not enough memory for icon upload");
                return;
            }
        }
        state->source_upload = source_upload;
        state->total = total;
        state->kind = ICON_KIND_COLOR;
        if (!source_upload) {
            if (!request->hasParam("id") || !request->hasParam("source_id") ||
                !request->hasParam("kind") || !request->hasParam("render_key")) {
                heap_caps_free(state->data);
                delete state;
                web_portal_send_json_error(request, 400, "Missing id, source_id, kind, or render_key parameter");
                return;
            }
            const String& key = request->getParam("id")->value();
            const String& source_id = request->getParam("source_id")->value();
            const String& kind = request->getParam("kind")->value();
            const String& render_key = request->getParam("render_key")->value();
            if (render_key.length() != 8 || strspn(render_key.c_str(), "0123456789abcdef") != 8 ||
                !(state->render_key = (uint32_t)strtoul(render_key.c_str(), nullptr, 16))) {
                heap_caps_free(state->data);
                delete state;
                web_portal_send_json_error(request, 400, "Invalid render_key");
                return;
            }
            if (!icon_store_key_valid(key.c_str()) || !icon_store_source_id_valid(source_id.c_str()) ||
                (kind != "0" && kind != "1")) {
                heap_caps_free(state->data);
                delete state;
                web_portal_send_json_error(request, 400, "Invalid icon key, source_id, or kind");
                return;
            }
            strlcpy(state->key, key.c_str(), sizeof(state->key));
            strlcpy(state->source_id, source_id.c_str(), sizeof(state->source_id));
            state->kind = kind == "1" ? ICON_KIND_MONO : ICON_KIND_COLOR;
        }
        request->_tempObject = state;
        request->onDisconnect([request]() { icon_upload_cleanup(request); });
    }

    auto* state = static_cast<IconUploadState*>(request->_tempObject);
    if (!state || state->source_upload != source_upload || state->total != total ||
        index != state->received || index > total || length > total - index ||
        (length && !data)) {
        icon_upload_cleanup(request);
        if (index == 0 || state)
            web_portal_send_json_error(request, 400, "Invalid icon upload framing");
        return;
    }
    if (state->png_source) {
        const IconStoreResult result = icon_store_source_png_write(state->stream, data, length);
        if (result != ICON_STORE_OK) {
            icon_upload_cleanup(request);
            icon_store_send_error(request, result);
            return;
        }
    } else {
        memcpy(state->data + index, data, length);
    }
    state->received += length;
    if (state->received != state->total) return;
    if (state->data) state->data[state->total] = '\0';

    if (source_upload) {
        char source_id[CONFIG_ICON_SOURCE_ID_MAX_LEN] = {};
        const IconStoreResult result = state->png_source
            ? icon_store_source_png_finish(state->stream, source_id, sizeof(source_id))
            : icon_store_source_install(state->data, state->total, source_id, sizeof(source_id));
        icon_upload_cleanup(request);
        if (result != ICON_STORE_OK) {
            icon_store_send_error(request, result);
            return;
        }
        char body[48];
        snprintf(body, sizeof(body), "{\"id\":\"%s\"}", source_id);
        request->send(201, "application/json", body);
        return;
    }

    const IconStoreResult result = icon_store_install(
        state->key, state->kind, state->source_id, state->render_key, state->data, state->total);
    icon_upload_cleanup(request);
    if (result != ICON_STORE_OK) {
        icon_store_send_error(request, result);
        return;
    }
    request->send(200, "application/json", "{\"success\":true}");
}

void handlePostIconInstall(AsyncWebServerRequest* request, uint8_t* data,
                           size_t length, size_t index, size_t total) {
    handleIconUploadBody(request, data, length, index, total, false);
}

void handlePostIconSource(AsyncWebServerRequest* request, uint8_t* data,
                          size_t length, size_t index, size_t total) {
    handleIconUploadBody(request, data, length, index, total, true);
}

void handleGetIconSource(AsyncWebServerRequest* request) {
    if (!portal_auth_gate(request)) return;
    if (!request->hasParam("id")) {
        web_portal_send_json_error(request, 400, "Missing source id parameter");
        return;
    }
    const String& source_id = request->getParam("id")->value();
    if (!icon_store_source_id_valid(source_id.c_str())) {
        web_portal_send_json_error(request, 400, "Invalid source id");
        return;
    }
    char path[64];
    bool png = false;
    if (!icon_store_source_path(source_id.c_str(), path, sizeof(path), &png)) {
        web_portal_send_json_error(request, 404, "Icon source not found");
        return;
    }
    sendFileThrottled(request, path, png ? "image/png" : "application/json");
}

// ============================================================================
// DELETE /api/icons/page?page=N
// ============================================================================
void handleDeletePageIcons(AsyncWebServerRequest *request) {
    if (!portal_auth_gate(request)) return;

    if (!request->hasParam("page")) {
        web_portal_send_json_error(request, 400, "Missing page parameter");
        return;
    }

    const String& val = request->getParam("page")->value();
    int page = val.toInt();
    if (page == 0 && val != "0") {
        web_portal_send_json_error(request, 400, "Invalid page parameter");
        return;
    }
    if (page < 0 || page >= MAX_PADS) {
        web_portal_send_json_error(request, 400, "Page out of range");
        return;
    }

    icon_store_delete_page_icons((uint8_t)page);
    request->send(200, "application/json", "{\"success\":true}");
}

// ============================================================================
// GET /api/icons/installed
// ============================================================================
void handleGetInstalledIcons(AsyncWebServerRequest *request) {
    if (!portal_auth_gate(request)) return;

    const bool scoped = request->hasParam("pages");
    bool pages[MAX_PADS] = {};
    if (scoped) {
        const String& value = request->getParam("pages")->value();
        const char* cursor = value.c_str();
        if (!value.length() || value.length() > MAX_PADS * 4) {
            web_portal_send_json_error(request, 400, "Invalid pages parameter");
            return;
        }
        while (*cursor) {
            char* end = nullptr;
            const unsigned long page = strtoul(cursor, &end, 10);
            if (*cursor < '0' || *cursor > '9' || page >= MAX_PADS ||
                (*end && (*end != ',' || !end[1]))) {
                web_portal_send_json_error(request, 400, "Invalid pages parameter");
                return;
            }
            pages[page] = true;
            cursor = *end ? end + 1 : end;
        }
    }

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    response->print(scoped ? "{\"artifacts\":{" : "{\"icons\":[");

    File dir = Storage.open("/icons");
    bool first = true;
    if (dir && dir.isDirectory()) {
        File entry = dir.openNextFile();
        while (entry) {
            const char* raw_name = entry.name();
            const char* name = raw_name ? strrchr(raw_name, '/') : nullptr;
            name = name ? name + 1 : raw_name;
            size_t nlen = name ? strlen(name) : 0;
            // Strip .png suffix for the ID
            size_t strip = 0;
            if (nlen > 4 && strcmp(name + nlen - 4, ".png") == 0) strip = 4;

            char key[CONFIG_ICON_KEY_MAX_LEN] = {};
            if (strip && nlen - strip < sizeof(key)) memcpy(key, name, nlen - strip);
            entry.close();
            unsigned page = 0;
            IconArtifactInfo info = {};
            const bool include = scoped
                ? icon_store_key_valid(key) && sscanf(key, "pad_%u_", &page) == 1 &&
                  page < MAX_PADS && pages[page] && icon_store_read_artifact(key, &info)
                : key[0] != '\0';
            if (include) {
                if (!first) response->print(",");
                response->print("\"");
                response->print(key);
                response->print("\"");
                if (scoped) {
                    response->printf(":{\"source_id\":\"%s\",\"kind\":%u,\"width\":%u,\"height\":%u,\"render_key\":\"%08x\"}",
                        info.source_id, (unsigned)info.kind, (unsigned)info.width,
                        (unsigned)info.height, (unsigned)info.render_key);
                }
                first = false;
            }
            entry = dir.openNextFile();
        }
        dir.close();
    }

    response->print(scoped ? "},\"cache_count\":" : "],\"cache_count\":");
    response->print(icon_store_cache_count());
    response->print("}");
    request->send(response);
}

// ============================================================================
// Debug endpoints
// ============================================================================

// GET /api/icons/files — list all files with full name and size
void handleGetIconFiles(AsyncWebServerRequest *request) {
    if (!portal_auth_gate(request)) return;

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    response->print("{\"files\":[");

    File dir = Storage.open("/icons");
    bool first = true;
    if (dir && dir.isDirectory()) {
        File entry = dir.openNextFile();
        while (entry) {
            if (!first) response->print(",");
            response->printf("{\"name\":\"%s\",\"size\":%u}",
                             entry.name(), (unsigned)entry.size());
            first = false;
            entry.close();
            entry = dir.openNextFile();
        }
        dir.close();
    }

    response->print("]}");
    request->send(response);
}

// GET /api/icons/cache — dump cache entries
void handleGetIconCache(AsyncWebServerRequest *request) {
    if (!portal_auth_gate(request)) return;

    AsyncResponseStream *response = request->beginResponseStream("application/json");

    struct Ctx {
        AsyncResponseStream* resp;
        bool first;
    } ctx = {response, true};

    response->print("{\"entries\":[");
    icon_store_enumerate_cache([](const IconCacheInfo* info, void* user) -> bool {
        Ctx* c = (Ctx*)user;
        if (!c->first) c->resp->print(",");
        c->resp->printf(
            "{\"id\":\"%s\",\"kind\":%u,\"w\":%u,\"h\":%u,\"data_size\":%u}",
            info->id, info->kind, info->w, info->h, info->data_size);
        c->first = false;
        return true;
    }, &ctx);
    response->print("],\"count\":");
    response->print(icon_store_cache_count());
    response->print("}");
    request->send(response);
}

// GET /api/icons/file?name=<filename> — download raw file from /icons/
void handleGetIconFile(AsyncWebServerRequest *request) {
    if (!portal_auth_gate(request)) return;

    if (!request->hasParam("name")) {
        web_portal_send_json_error(request, 400, "Missing name parameter");
        return;
    }

    const String& name = request->getParam("name")->value();

    // Sanitize: reject path traversal
    if (name.indexOf("..") >= 0 || name.indexOf('/') >= 0 || name.length() == 0) {
        web_portal_send_json_error(request, 400, "Invalid filename");
        return;
    }

    char path[64];
    snprintf(path, sizeof(path), "/icons/%s", name.c_str());

    if (!Storage.exists(path)) {
        web_portal_send_json_error(request, 404, "File not found");
        return;
    }

    // Determine content type from extension
    const char* ct = "application/octet-stream";
    if (name.endsWith(".png")) ct = "image/png";

    // Use throttled streamer to avoid WiFi TX buffer exhaustion on ESP-Hosted
    // (same pattern as the FsIndexedStore JSON path).
    sendFileThrottled(request, path, ct);
}

// DELETE /api/icons/file?name=<filename> — delete a specific file
void handleDeleteIconFile(AsyncWebServerRequest *request) {
    if (!portal_auth_gate(request)) return;

    if (!request->hasParam("name")) {
        web_portal_send_json_error(request, 400, "Missing name parameter");
        return;
    }

    const String& name = request->getParam("name")->value();

    // Sanitize: reject path traversal
    if (name.indexOf("..") >= 0 || name.indexOf('/') >= 0 || name.length() == 0) {
        web_portal_send_json_error(request, 400, "Invalid filename");
        return;
    }

    char path[64];
    snprintf(path, sizeof(path), "/icons/%s", name.c_str());

    if (!Storage.exists(path)) {
        web_portal_send_json_error(request, 404, "File not found");
        return;
    }

    if (!icon_store_delete_file(name.c_str())) {
        web_portal_send_json_error(request, 500, "Failed to delete generated icon");
        return;
    }
    LOGI(TAG, "Deleted: %s", path);
    request->send(200, "application/json", "{\"success\":true}");
}

// ============================================================================
// GET /api/pad/button_sizes?cols=N&rows=N
// ============================================================================
void handleGetButtonSizes(AsyncWebServerRequest *request) {
    if (!portal_auth_gate(request)) return;

    if (!request->hasParam("cols") || !request->hasParam("rows")) {
        web_portal_send_json_error(request, 400, "Missing cols or rows parameter");
        return;
    }

    int cols = request->getParam("cols")->value().toInt();
    int rows = request->getParam("rows")->value().toInt();
    if (cols < 1 || cols > MAX_GRID_COLS || rows < 1 || rows > MAX_GRID_ROWS) {
        web_portal_send_json_error(request, 400, "cols/rows must be 1-8");
        return;
    }

    // Get display dimensions
    int disp_w = DISPLAY_WIDTH;
    int disp_h = DISPLAY_HEIGHT;
    if (displayManager && displayManager->getDriver()) {
        disp_w = displayManager->getDriver()->width();
        disp_h = displayManager->getDriver()->height();
    }

    // Compute a single 1×1 tile (no spans) to get base dimensions
    uint8_t col_arr[1] = {0};
    uint8_t row_arr[1] = {0};
    uint8_t cs_arr[1] = {1};
    uint8_t rs_arr[1] = {1};
    const PadLayoutSettings& settings = button_defaults_get()->layout;
    const PadGridLayoutSpace layout_space = {
        settings.inset_left_px, settings.inset_right_px,
        settings.inset_top_px, settings.inset_bottom_px,
        settings.spacing_px, settings.spacing_px,
        settings.pixel_shift_distance_px,
    };
    PadRect rect;

    pad_compute_grid((uint8_t)cols, (uint8_t)rows,
                     (uint16_t)disp_w, (uint16_t)disp_h,
                     col_arr, row_arr, cs_arr, rs_arr, 1, &rect, &layout_space);

    const UIScaleInfo& scale = pad_get_scale_info();
    const uint8_t padding = 4;  // Matches lv_obj_set_style_pad_all(obj, 4, 0)
    if (displayManager) displayManager->lock();
    const uint32_t font_small_h = lv_font_get_line_height(pad_typography_font(scale.font_small));
    if (displayManager) displayManager->unlock();

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    response->printf(
        "{\"display_w\":%d,\"display_h\":%d,"
        "\"button_w\":%u,\"button_h\":%u,"
        "\"gap\":%u,\"padding\":%u,"
        "\"pixel_shift_margin\":%u,"
        "\"font_small_h\":%u}",
        disp_w, disp_h,
        rect.w, rect.h,
        settings.spacing_px, padding,
        settings.pixel_shift_distance_px,
        font_small_h);
    request->send(response);
}

#endif // HAS_DISPLAY
