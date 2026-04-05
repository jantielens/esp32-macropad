#include "web_portal_sounds.h"

#if HAS_SOUND_PLAYER

#include "audio.h"
#include "log_manager.h"
#include "sound_store.h"
#include "web_portal_auth.h"
#include "web_portal_json.h"

#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <string.h>

#define TAG "SoundAPI"

// ---------------------------------------------------------------------------
// MP3 header validation
// ---------------------------------------------------------------------------
// Checks if the data starts with a valid MP3 sync word or ID3v2 tag.
// MP3 frame sync: 0xFF followed by 0xE0+ (11-bit sync, MPEG audio)
// ID3v2 tag: starts with "ID3" (metadata header before audio frames)
static bool validate_mp3_header(const uint8_t* data, size_t len) {
    if (!data || len < 4) return false;

    // Check for ID3v2 tag header
    if (data[0] == 'I' && data[1] == 'D' && data[2] == '3') return true;

    // Check for MP3 frame sync: first byte 0xFF, second byte has upper 3 bits set (0xE0 mask)
    if (data[0] == 0xFF && (data[1] & 0xE0) == 0xE0) return true;

    return false;
}

// ---------------------------------------------------------------------------
// Body accumulator for POST /api/sounds/upload
// ---------------------------------------------------------------------------
static struct {
    bool in_progress;
    bool errored;
    uint32_t started_ms;
    size_t total;
    size_t received;
    uint8_t* buf;
    char name[SOUND_NAME_MAX_LEN];
} g_sound_post = {false, false, 0, 0, 0, nullptr, {0}};

static void sound_post_reset() {
    if (g_sound_post.buf) {
        heap_caps_free(g_sound_post.buf);
        g_sound_post.buf = nullptr;
    }
    g_sound_post.in_progress = false;
    g_sound_post.errored = false;
    g_sound_post.total = 0;
    g_sound_post.received = 0;
    g_sound_post.started_ms = 0;
    g_sound_post.name[0] = '\0';
}

// ============================================================================
// POST /api/sounds/upload?name=<name>
// ============================================================================
void handlePostSoundUpload(AsyncWebServerRequest *request, uint8_t *data,
                           size_t len, size_t index, size_t total) {
    if (!portal_auth_gate(request)) return;

    if (index != 0 && g_sound_post.errored) {
        return;
    }

    if (index == 0) {
        g_sound_post.errored = false;
        // First chunk — parse name param, allocate buffer
        if (!request->hasParam("name")) {
            web_portal_send_json_error(request, 400, "Missing name parameter");
            g_sound_post.errored = true;
            return;
        }
        const String& name_val = request->getParam("name")->value();
        if (!sound_store_validate_name(name_val.c_str())) {
            web_portal_send_json_error(request, 400, "Invalid sound name (a-z, A-Z, 0-9, _, - only)");
            g_sound_post.errored = true;
            return;
        }

        // Cleanup stuck upload
        const uint32_t now = millis();
        if (g_sound_post.in_progress && g_sound_post.started_ms &&
            (now - g_sound_post.started_ms > 10000)) {
            LOGW(TAG, "Stuck sound upload — resetting");
            sound_post_reset();
        }

        if (g_sound_post.in_progress) {
            web_portal_send_json_error(request, 409, "Sound upload already in progress");
            g_sound_post.errored = true;
            return;
        }

        if (total == 0 || total > SOUND_MAX_FILE_SIZE) {
            LOGE(TAG, "Sound file size rejected: %u bytes", (unsigned)total);
            web_portal_send_json_error(request, 413, "Sound file too large or empty");
            g_sound_post.errored = true;
            return;
        }

        // Allocate in PSRAM if available
        uint8_t* buf = nullptr;
        if (psramFound()) {
            buf = (uint8_t*)heap_caps_malloc(total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        }
        if (!buf) {
            buf = (uint8_t*)heap_caps_malloc(total, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        if (!buf) {
            LOGE(TAG, "Failed to allocate %u bytes for upload", (unsigned)total);
            web_portal_send_json_error(request, 507, "Out of memory");
            g_sound_post.errored = true;
            return;
        }

        strlcpy(g_sound_post.name, name_val.c_str(), SOUND_NAME_MAX_LEN);
        g_sound_post.buf = buf;
        g_sound_post.total = total;
        g_sound_post.received = 0;
        g_sound_post.in_progress = true;
        g_sound_post.started_ms = millis();
    }

    if (!g_sound_post.in_progress) return;

    // Accumulate chunk
    size_t space = g_sound_post.total - g_sound_post.received;
    size_t copy_len = (len > space) ? space : len;
    memcpy(g_sound_post.buf + g_sound_post.received, data, copy_len);
    g_sound_post.received += copy_len;

    // Final chunk?
    if (g_sound_post.received >= g_sound_post.total) {
        // Validate MP3 format before saving
        if (!validate_mp3_header(g_sound_post.buf, g_sound_post.total)) {
            LOGW(TAG, "Rejected upload '%s': not a valid MP3 file", g_sound_post.name);
            sound_post_reset();
            web_portal_send_json_error(request, 400, "Not a valid MP3 file");
            return;
        }

        bool ok = sound_store_save(g_sound_post.name, g_sound_post.buf, g_sound_post.total);
        sound_post_reset();

        if (ok) {
            request->send(200, "application/json", "{\"status\":\"ok\"}");
        } else {
            web_portal_send_json_error(request, 500, "Failed to save sound file");
        }
    }
}

// ============================================================================
// GET /api/sounds/list
// ============================================================================
void handleGetSoundList(AsyncWebServerRequest *request) {
    if (!portal_auth_gate(request)) return;

    char names[SOUND_LIST_MAX][SOUND_NAME_MAX_LEN];
    int count = sound_store_list(names, SOUND_LIST_MAX);

    StaticJsonDocument<2048> doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < count; i++) {
        arr.add(names[i]);
    }

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}

// ============================================================================
// DELETE /api/sounds?name=<name>
// ============================================================================
void handleDeleteSound(AsyncWebServerRequest *request) {
    if (!portal_auth_gate(request)) return;

    if (!request->hasParam("name")) {
        web_portal_send_json_error(request, 400, "Missing name parameter");
        return;
    }

    const String& name = request->getParam("name")->value();
    if (sound_store_delete(name.c_str())) {
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    } else {
        web_portal_send_json_error(request, 404, "Sound not found");
    }
}

// ============================================================================
// POST /api/sounds/play?name=<name>
// ============================================================================
void handlePostSoundPlay(AsyncWebServerRequest *request) {
    if (!portal_auth_gate(request)) return;

    if (!request->hasParam("name")) {
        web_portal_send_json_error(request, 400, "Missing name parameter");
        return;
    }

    const String& name = request->getParam("name")->value();
    if (!sound_store_exists(name.c_str())) {
        web_portal_send_json_error(request, 404, "Sound not found");
        return;
    }

    audio_play_sound(name.c_str(), 0);
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

#endif // HAS_SOUND_PLAYER
