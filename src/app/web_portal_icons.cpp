#include "web_portal_icons.h"

#if HAS_DISPLAY

#include "board_config.h"
#include "display_manager.h"
#include "icon_store.h"
#include "log_manager.h"
#include "pad_layout.h"
#include "web_portal_auth.h"
#include "web_portal_json.h"

#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <string.h>

#define TAG "IconAPI"

// Maximum icon blob size accepted via POST
#define ICON_POST_MAX_BYTES ICON_MAX_PNG_SIZE

// Body accumulator for POST /api/icons/install
static struct {
    bool in_progress;
    bool errored;       // Error already sent — ignore subsequent chunks
    uint32_t started_ms;
    size_t total;
    size_t received;
    uint8_t* buf;
    char id[CONFIG_ICON_ID_MAX_LEN];
    IconKind kind;
} g_icon_post = {false, false, 0, 0, 0, nullptr, {0}, ICON_KIND_COLOR};

static void icon_post_reset() {
    if (g_icon_post.buf) {
        heap_caps_free(g_icon_post.buf);
        g_icon_post.buf = nullptr;
    }
    g_icon_post.in_progress = false;
    g_icon_post.errored = false;
    g_icon_post.total = 0;
    g_icon_post.received = 0;
    g_icon_post.started_ms = 0;
    g_icon_post.id[0] = '\0';
    g_icon_post.kind = ICON_KIND_COLOR;
}

// Validate icon ID: only [a-z0-9_] allowed, max length
static bool validate_icon_id(const char* id) {
    if (!id || !id[0]) return false;
    size_t len = strlen(id);
    if (len >= CONFIG_ICON_ID_MAX_LEN) return false;
    for (size_t i = 0; i < len; i++) {
        char c = id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    return true;
}

// ============================================================================
// POST /api/icons/install?id=<id>
// ============================================================================
void handlePostIconInstall(AsyncWebServerRequest *request, uint8_t *data,
                           size_t len, size_t index, size_t total) {
    if (!portal_auth_gate(request)) return;

    // If a previous chunk of THIS request already sent an error, ignore remaining chunks.
    // At index == 0 (new request), always clear the flag so stale errors from a
    // prior request don't block this one.
    if (index == 0) {
        g_icon_post.errored = false;
    } else if (g_icon_post.errored) {
        return;
    }

    if (index == 0) {
        // First chunk — parse id param, allocate buffer
        if (!request->hasParam("id")) {
            web_portal_send_json_error(request, 400, "Missing id parameter");
            g_icon_post.errored = true;
            return;
        }
        const String& id_val = request->getParam("id")->value();
        if (!validate_icon_id(id_val.c_str())) {
            web_portal_send_json_error(request, 400, "Invalid icon ID (a-z0-9_ only)");
            g_icon_post.errored = true;
            return;
        }

        // Cleanup stuck upload (e.g., client disconnected mid-transfer)
        const uint32_t now = millis();
        if (g_icon_post.in_progress && g_icon_post.started_ms &&
            (now - g_icon_post.started_ms > 5000)) {
            LOGW(TAG, "Stuck icon upload — resetting");
            icon_post_reset();
        }

        if (g_icon_post.in_progress) {
            web_portal_send_json_error(request, 409, "Icon upload already in progress");
            g_icon_post.errored = true;
            return;
        }

        if (total == 0 || total > ICON_POST_MAX_BYTES) {
            LOGE(TAG, "Icon blob size rejected: %u bytes", (unsigned)total);
            web_portal_send_json_error(request, 413, "Icon blob too large or empty");
            g_icon_post.errored = true;
            return;
        }

        uint8_t* buf = nullptr;
        if (psramFound()) {
            buf = (uint8_t*)heap_caps_malloc(total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        }
        if (!buf) {
            buf = (uint8_t*)heap_caps_malloc(total, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        if (!buf) {
            web_portal_send_json_error(request, 503, "Out of memory for icon");
            g_icon_post.errored = true;
            return;
        }

        LOGI(TAG, "Icon upload start: id=%s total=%u", id_val.c_str(), (unsigned)total);
        g_icon_post.in_progress = true;
        g_icon_post.started_ms = now;
        g_icon_post.total = total;
        g_icon_post.received = 0;
        g_icon_post.buf = buf;
        strlcpy(g_icon_post.id, id_val.c_str(), CONFIG_ICON_ID_MAX_LEN);
        g_icon_post.kind = ICON_KIND_COLOR;
        if (request->hasParam("kind")) {
            int k = request->getParam("kind")->value().toInt();
            if (k == 1) g_icon_post.kind = ICON_KIND_MONO;
        }
    }

    // Copy chunk
    if (!g_icon_post.in_progress || !g_icon_post.buf ||
        g_icon_post.total != total || (index + len) > total) {
        LOGE(TAG, "Icon upload bad state: prog=%d buf=%p total=%u/%u idx=%u len=%u",
             g_icon_post.in_progress, g_icon_post.buf,
             (unsigned)g_icon_post.total, (unsigned)total,
             (unsigned)index, (unsigned)len);
        web_portal_send_json_error(request, 400, "Icon upload state mismatch");
        icon_post_reset();
        return;
    }

    memcpy(g_icon_post.buf + index, data, len);

    size_t new_received = index + len;
    if (new_received > g_icon_post.received) {
        g_icon_post.received = new_received;
    }

    if (g_icon_post.received < g_icon_post.total) {
        return;  // More chunks to come
    }

    // All data received — install
    bool ok = icon_store_install(g_icon_post.id, g_icon_post.kind,
                                 g_icon_post.buf, g_icon_post.total);
    icon_post_reset();

    if (!ok) {
        web_portal_send_json_error(request, 400, "Invalid PNG data");
        return;
    }

    request->send(200, "application/json", "{\"success\":true}");
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
    if (page < 0 || page >= MAX_PAD_PAGES) {
        web_portal_send_json_error(request, 400, "Page must be 0-7");
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

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    response->print("{\"icons\":[");

    File dir = LittleFS.open("/icons");
    bool first = true;
    if (dir && dir.isDirectory()) {
        File entry = dir.openNextFile();
        while (entry) {
            const char* name = entry.name();
            size_t nlen = name ? strlen(name) : 0;
            // Strip .png suffix for the ID
            size_t strip = 0;
            if (nlen > 4 && strcmp(name + nlen - 4, ".png") == 0) strip = 4;

            if (strip) {
                if (!first) response->print(",");
                response->print("\"");
                for (size_t i = 0; i < nlen - strip; i++) {
                    response->print(name[i]);
                }
                response->print("\"");
                first = false;
            }
            entry.close();
            entry = dir.openNextFile();
        }
        dir.close();
    }

    response->print("],\"cache_count\":");
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

    File dir = LittleFS.open("/icons");
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

    if (!LittleFS.exists(path)) {
        web_portal_send_json_error(request, 404, "File not found");
        return;
    }

    // Determine content type from extension
    const char* ct = "application/octet-stream";
    if (name.endsWith(".png")) ct = "image/png";

    request->send(LittleFS, path, ct);
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

    if (!LittleFS.exists(path)) {
        web_portal_send_json_error(request, 404, "File not found");
        return;
    }

    LittleFS.remove(path);
    LOGI(TAG, "Deleted: %s", path);
    request->send(200, "application/json", "{\"success\":true}");
}

// ============================================================================
// GET /api/pad/tile_sizes?cols=N&rows=N
// ============================================================================
void handleGetTileSizes(AsyncWebServerRequest *request) {
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
    PadRect rect;

    pad_compute_grid((uint8_t)cols, (uint8_t)rows,
                     (uint16_t)disp_w, (uint16_t)disp_h,
                     col_arr, row_arr, cs_arr, rs_arr, 1, &rect);

    const UIScaleInfo& scale = pad_get_scale_info();
    const uint8_t padding = 4;  // Matches lv_obj_set_style_pad_all(obj, 4, 0)
    const uint8_t font_small_h = lv_font_get_line_height(scale.font_small);

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    response->printf(
        "{\"display_w\":%d,\"display_h\":%d,"
        "\"tile_w\":%u,\"tile_h\":%u,"
        "\"gap\":%u,\"padding\":%u,"
        "\"pixel_shift_margin\":%u,"
        "\"font_small_h\":%u}",
        disp_w, disp_h,
        rect.w, rect.h,
        scale.gap, padding,
        PIXEL_SHIFT_MARGIN,
        font_small_h);
    request->send(response);
}

#endif // HAS_DISPLAY
