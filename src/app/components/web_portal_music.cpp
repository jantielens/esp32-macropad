#include "../web_portal_music.h"

#if HAS_SOUND_PLAYER

#include "../audio.h"
#include "../music_catalog.h"
#include "../sound_player.h"
#include "../storage.h"
#include "../web_portal_auth.h"
#include "../web_portal_json.h"
#include "../web_portal_routes.h"

#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <string.h>

namespace {

constexpr size_t MUSIC_CATALOG_JSON_CAPACITY =
    static_cast<size_t>(MUSIC_TRACK_LIMIT) * MUSIC_PATH_MAX_LEN + 256;
constexpr uint32_t MUSIC_WORKER_TIMEOUT_MS = 60000;
constexpr uint32_t MUSIC_FINALIZE_TASK_STACK_SIZE = 6144;

enum MusicUploadPhase : uint8_t {
    MUSIC_UPLOAD_IDLE,
    MUSIC_UPLOAD_VALIDATING,
    MUSIC_UPLOAD_REFRESHING,
    MUSIC_UPLOAD_COMPLETE,
    MUSIC_UPLOAD_ERROR,
};

struct MusicUploadState {
    AsyncWebServerRequest* request;
    File file;
    char destination[MUSIC_PATH_MAX_LEN];
    char temporary[MUSIC_PATH_MAX_LEN + 32];
    size_t total;
    size_t received;
    bool active;
    bool finalizing;
};

MusicUploadState g_music_upload = {};
TaskHandle_t g_music_finalize_task = nullptr;
portMUX_TYPE g_music_upload_status_mux = portMUX_INITIALIZER_UNLOCKED;
MusicUploadPhase g_music_upload_phase = MUSIC_UPLOAD_IDLE;
char g_music_upload_error[96] = {};

const char* music_upload_phase_name(MusicUploadPhase phase) {
    switch (phase) {
        case MUSIC_UPLOAD_IDLE: return "idle";
        case MUSIC_UPLOAD_VALIDATING: return "validating";
        case MUSIC_UPLOAD_REFRESHING: return "refreshing";
        case MUSIC_UPLOAD_COMPLETE: return "complete";
        case MUSIC_UPLOAD_ERROR: return "error";
    }
    return "error";
}

void music_upload_set_status(MusicUploadPhase phase, const char* error = nullptr) {
    portENTER_CRITICAL(&g_music_upload_status_mux);
    g_music_upload_phase = phase;
    strlcpy(g_music_upload_error, error ? error : "", sizeof(g_music_upload_error));
    portEXIT_CRITICAL(&g_music_upload_status_mux);
}

bool ensure_music_parent_directories(const char* path) {
    char parent[MUSIC_PATH_MAX_LEN] = {};
    strlcpy(parent, path, sizeof(parent));
    char* final_separator = strrchr(parent, '/');
    if (!final_separator || final_separator == parent) return false;
    *final_separator = '\0';
    for (char* separator = parent + 1; *separator; ++separator) {
        if (*separator != '/') continue;
        *separator = '\0';
        const bool exists = Storage.exists(parent);
        const bool created = exists || Storage.mkdir(parent);
        *separator = '/';
        if (!created) return false;
    }
    return Storage.exists(parent) || Storage.mkdir(parent);
}

void music_upload_cleanup(bool release_mutation = true, bool catalog_changed = false) {
    if (g_music_upload.file) g_music_upload.file.close();
    if (g_music_upload.temporary[0]) Storage.remove(g_music_upload.temporary);
    if (release_mutation && g_music_upload.active) audio_music_storage_mutation_end(catalog_changed);
    g_music_upload = {};
}

void music_upload_fail(AsyncWebServerRequest* request, int status, const char* message) {
    music_upload_cleanup();
    music_upload_set_status(MUSIC_UPLOAD_ERROR, message);
    web_portal_send_json_error(request, status, message);
}

bool music_upload_start(AsyncWebServerRequest* request, size_t total) {
    if (!request->hasParam("path")) {
        web_portal_send_json_error(request, 400, "Missing path parameter");
        return false;
    }
    const String path = request->getParam("path")->value();
    if (!MusicCatalog::is_canonical_path(path.c_str())) {
        web_portal_send_json_error(request, 400, "Invalid music path");
        return false;
    }
    if (total == 0) {
        web_portal_send_json_error(request, 400, "Empty upload");
        return false;
    }
    if (!audio_music_storage_mutation_begin()) {
        web_portal_send_json_error(request, 409, "Music storage is busy");
        return false;
    }

    uint8_t catalog_count = 0;
    const bool catalog_available = audio_get_music_catalog_count(&catalog_count);
    // A missing /media directory is the empty-library state before the first
    // upload. The transaction below creates it; an existing but unpublished
    // directory still indicates a discovery failure and remains unavailable.
    if (!catalog_available && Storage.exists("/media")) {
        audio_music_storage_mutation_end(false);
        web_portal_send_json_error(request, 503, "Music catalog unavailable");
        return false;
    }
    if (Storage.exists(path.c_str())) {
        audio_music_storage_mutation_end(false);
        web_portal_send_json_error(request, 409, "Music file already exists");
        return false;
    }
    if (!ensure_music_parent_directories(path.c_str())) {
        audio_music_storage_mutation_end(false);
        web_portal_send_json_error(request, 500, "Unable to create media directory");
        return false;
    }

    g_music_upload.active = true;
    g_music_upload.finalizing = false;
    music_upload_set_status(MUSIC_UPLOAD_IDLE);
    g_music_upload.request = request;
    g_music_upload.total = total;
    strlcpy(g_music_upload.destination, path.c_str(), sizeof(g_music_upload.destination));
    for (uint8_t attempt = 0; attempt < 4; ++attempt) {
        snprintf(g_music_upload.temporary, sizeof(g_music_upload.temporary),
                 "%s.upload-%08lx.tmp", g_music_upload.destination,
                 (unsigned long)esp_random());
        if (!Storage.exists(g_music_upload.temporary)) break;
    }
    if (Storage.exists(g_music_upload.temporary)) {
        music_upload_fail(request, 500, "Unable to create temporary upload");
        return false;
    }
    g_music_upload.file = Storage.open(g_music_upload.temporary, "w");
    if (!g_music_upload.file) {
        music_upload_fail(request, 500, "Unable to create temporary upload");
        return false;
    }
    request->onDisconnect([request]() {
        if (g_music_upload.active && !g_music_upload.finalizing &&
            g_music_upload.request == request) {
            music_upload_cleanup();
        }
    });
    return true;
}

void music_upload_finalize_task(void*) {
    bool valid = false;
    music_upload_set_status(MUSIC_UPLOAD_VALIDATING);
    if (!audio_music_validate_path(g_music_upload.temporary, MUSIC_WORKER_TIMEOUT_MS, &valid)) {
        music_upload_set_status(MUSIC_UPLOAD_ERROR, "Music validation timed out");
        music_upload_cleanup();
    } else if (!valid) {
        music_upload_set_status(MUSIC_UPLOAD_ERROR, "Upload contains no valid MP3 stream");
        music_upload_cleanup();
    } else if (Storage.exists(g_music_upload.destination)) {
        music_upload_set_status(MUSIC_UPLOAD_ERROR, "Music file already exists");
        music_upload_cleanup();
    } else if (!Storage.rename(g_music_upload.temporary, g_music_upload.destination)) {
        music_upload_set_status(MUSIC_UPLOAD_ERROR, "Unable to publish music file");
        music_upload_cleanup();
    } else {
        g_music_upload.temporary[0] = '\0';
        music_upload_set_status(MUSIC_UPLOAD_REFRESHING);
        const bool refreshed = audio_music_refresh_catalog(MUSIC_WORKER_TIMEOUT_MS);
        // Keep the mutation reservation through refresh, then release once.
        music_upload_cleanup(false);
        audio_music_storage_mutation_end(!refreshed);
        music_upload_set_status(refreshed ? MUSIC_UPLOAD_COMPLETE : MUSIC_UPLOAD_ERROR,
                                refreshed ? nullptr : "Music catalog refresh timed out");
    }
    g_music_finalize_task = nullptr;
    vTaskDelete(nullptr);
}

void handlePostMusicUpload(AsyncWebServerRequest* request, uint8_t* data,
                           size_t length, size_t index, size_t total) {
    if (!portal_auth_gate(request)) {
        if (g_music_upload.active && g_music_upload.request == request) music_upload_cleanup();
        return;
    }
    if (index == 0) {
        if (g_music_upload.active) {
            web_portal_send_json_error(request, 409, "Music upload already in progress");
            return;
        }
        if (!music_upload_start(request, total)) return;
    }
    if (!g_music_upload.active || g_music_upload.request != request) return;
    if (index != g_music_upload.received || length > g_music_upload.total - g_music_upload.received ||
        g_music_upload.file.write(data, length) != length) {
        music_upload_fail(request, 500, "Music upload write failed");
        return;
    }
    g_music_upload.received += length;
    if (g_music_upload.received != g_music_upload.total) return;

    g_music_upload.file.close();
    g_music_upload.finalizing = true;
    if (xTaskCreate(music_upload_finalize_task, "music_upload", MUSIC_FINALIZE_TASK_STACK_SIZE,
                    nullptr, 2, &g_music_finalize_task) != pdPASS) {
        g_music_upload.finalizing = false;
        music_upload_fail(request, 503, "Unable to start Music validation");
        return;
    }
    request->send(202, "application/json", "{\"accepted\":true,\"state\":\"validating\"}");
}

} // namespace

void handleGetMusicCatalog(AsyncWebServerRequest* request) {
    if (!portal_auth_gate(request)) return;

    MusicCatalogSnapshot* snapshot = static_cast<MusicCatalogSnapshot*>(
        heap_caps_malloc(sizeof(MusicCatalogSnapshot),
                         psramFound() ? MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
                                      : MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!snapshot) {
        web_portal_send_json_error(request, 503, "Music catalog response allocation failed");
        return;
    }
    MusicCatalogStatus status = {};
    audio_get_music_catalog_snapshot(snapshot);
    audio_get_music_catalog_status(&status);

    auto doc = make_psram_json_doc(MUSIC_CATALOG_JSON_CAPACITY);
    if (!doc || doc->capacity() == 0) {
        heap_caps_free(snapshot);
        web_portal_send_json_error(request, 503, "Music catalog response allocation failed");
        return;
    }
    JsonObject response = doc->to<JsonObject>();
    JsonArray files = response["files"].to<JsonArray>();
    for (uint8_t index = 0; index < snapshot->count; ++index) {
        files.add(snapshot->paths[index]);
    }
    response["count"] = snapshot->count;
    response["limit"] = MUSIC_TRACK_LIMIT;
    response["available"] = snapshot->available;
    response["complete"] = snapshot->available;
    response["overflow"] = snapshot->overflow;
    response["total_found"] = snapshot->total_found;
    response["skipped"] = snapshot->skipped;
    response["generation"] = status.generation;
    response["stale"] = status.stale;
    response["last_refresh_result"] = status.last_refresh_result;
    heap_caps_free(snapshot);
    if (doc->overflowed()) {
        web_portal_send_json_error(request, 500, "Music catalog response overflow");
        return;
    }
    web_portal_send_json_chunked(request, doc);
}

void handleGetMusicUploadStatus(AsyncWebServerRequest* request) {
    if (!portal_auth_gate(request)) return;
    MusicUploadPhase phase;
    char error[sizeof(g_music_upload_error)];
    portENTER_CRITICAL(&g_music_upload_status_mux);
    phase = g_music_upload_phase;
    strlcpy(error, g_music_upload_error, sizeof(error));
    portEXIT_CRITICAL(&g_music_upload_status_mux);
    String response = String("{\"state\":\"") + music_upload_phase_name(phase) +
        "\",\"in_progress\":" +
        ((phase == MUSIC_UPLOAD_VALIDATING || phase == MUSIC_UPLOAD_REFRESHING) ? "true" : "false") +
        ",\"error\":\"" + error + "\"}";
    request->send(200, "application/json", response);
}

void handleDeleteMusic(AsyncWebServerRequest* request) {
    if (!portal_auth_gate(request)) return;
    if (!request->hasParam("path")) {
        web_portal_send_json_error(request, 400, "Missing path parameter");
        return;
    }
    const String path = request->getParam("path")->value();
    if (!MusicCatalog::is_canonical_path(path.c_str())) {
        web_portal_send_json_error(request, 400, "Invalid music path");
        return;
    }
    if (!audio_music_storage_mutation_begin()) {
        web_portal_send_json_error(request, 409, "Music storage is busy");
        return;
    }
    File target = Storage.open(path.c_str(), "r");
    if (!target || target.isDirectory()) {
        if (target) target.close();
        audio_music_storage_mutation_end(false);
        web_portal_send_json_error(request, 404, "Music file not found");
        return;
    }
    target.close();
    if (!Storage.remove(path.c_str())) {
        audio_music_storage_mutation_end(false);
        web_portal_send_json_error(request, 500, "Unable to delete music file");
        return;
    }
    const bool refreshed = audio_music_refresh_catalog(5000);
    audio_music_storage_mutation_end(!refreshed);
    if (!refreshed) {
        web_portal_send_json_error(request, 503, "Music catalog refresh unavailable");
        return;
    }
    request->send(200, "application/json", "{\"success\":true}");
}

static void music_routes_register(AsyncWebServer* server) {
    server->on("/api/music", HTTP_GET, handleGetMusicCatalog);
    server->on("/api/music/upload/status", HTTP_GET, handleGetMusicUploadStatus);
    server->on("/api/music", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!portal_auth_gate(request)) return;
        if (request->contentLength() == 0) {
            web_portal_send_json_error(request, 400, "Empty upload");
        }
    }, nullptr,
               handlePostMusicUpload);
    server->on("/api/music", HTTP_DELETE, handleDeleteMusic);
}

REGISTER_ROUTES(music_routes_register)

#endif // HAS_SOUND_PLAYER