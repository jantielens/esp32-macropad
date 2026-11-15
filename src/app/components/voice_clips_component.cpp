#include "component_registry.h"
#include "voice_clips.h"
#include "voice_clips_file.h"
#include "voice_clip_range.h"
#include "fs_health.h"
#include "web_portal_json.h"
#include "web_portal_utils.h"
#include "log_manager.h"
#include <ArduinoJson.h>
#include <memory>
#include <new>

namespace {
bool voice_clip_id_valid(const String& id) {
    if (id.length() != 16) return false;
    for (size_t i = 0; i < id.length(); ++i) {
        const char c = id[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

bool voice_clip_file_error(AsyncWebServerRequest* request, VoiceClipFileStatus status) {
    if (status == VoiceClipFileStatus::Ready) return false;
    const int code = status == VoiceClipFileStatus::NotFound ? 404 :
                     status == VoiceClipFileStatus::Busy ? 409 : 503;
    const char* message = status == VoiceClipFileStatus::NotFound ? "Clip not found" :
                          status == VoiceClipFileStatus::Busy ? "Clip is busy" :
                          "Clip storage unavailable";
    web_portal_send_json_error(request, code, message);
    return true;
}

void voice_clips_list_get(AsyncWebServerRequest* request) {
    const String list = voice_clips_list();
    if (list.isEmpty()) {
        web_portal_send_json_error(request, 503, "Clip catalog unavailable");
        return;
    }
    request->send(200, "application/json", list);
}

void voice_clips_status_get(AsyncWebServerRequest* request) {
    auto doc = make_psram_json_doc(512);
    if (!doc) {
        web_portal_send_json_error(request, 503, "Out of memory");
        return;
    }
    VoiceClipSnapshot snapshot{};
    voice_clips_snapshot(&snapshot);
    (*doc)["state"] = voice_clips_state_name(snapshot.state);
    (*doc)["error"] = snapshot.error;
    (*doc)["duration_ms"] = snapshot.duration_ms;
#if USE_SD_STORAGE && defined(SD_STORAGE_SPI) && SD_STORAGE_SPI
    (*doc)["backend"] = "sd_spi";
#elif USE_SD_STORAGE
    (*doc)["backend"] = "sdmmc";
#else
    (*doc)["backend"] = "littlefs";
#endif
    FSHealthStats storage{};
    fs_health_get(&storage);
    const uint64_t total = storage.storage_total_bytes;
    const uint64_t used = storage.storage_used_bytes;
    (*doc)["total_bytes"] = total;
    (*doc)["free_bytes"] = total >= used ? total - used : 0;
    web_portal_send_json_sized(request, doc);
}

struct VoiceClipStream {
    File file;
    char id[32] = {};
    ~VoiceClipStream() { if (file) voice_clips_close(&file, id); }
};

void voice_clips_file_get(AsyncWebServerRequest* request) {
    const String id = request->hasParam("id") ? request->getParam("id")->value() : "";
    if (!voice_clip_id_valid(id)) {
        web_portal_send_json_error(request, 400, "Invalid clip ID");
        return;
    }
    auto stream = std::make_shared<VoiceClipStream>();
    strlcpy(stream->id, id.c_str(), sizeof(stream->id));
    if (voice_clip_file_error(request, voice_clips_open(id.c_str(), &stream->file))) return;
    const uint64_t size = stream->file.size();
    uint64_t start = 0, end = size ? size - 1 : 0;
    const bool partial = request->hasHeader("Range");
    if (partial && !voice_clip_parse_range(request->getHeader("Range")->value().c_str(),
                                            size, &start, &end)) {
        AsyncWebServerResponse* response = request->beginResponse(416, "text/plain", "");
        char range[48];
        snprintf(range, sizeof(range), "bytes */%llu", static_cast<unsigned long long>(size));
        response->addHeader("Content-Range", range);
        response->addHeader("Accept-Ranges", "bytes");
        request->send(response);
        return;
    }
    if (!size || !stream->file.seek(static_cast<size_t>(start))) {
        web_portal_send_json_error(request, 500, "Clip seek failed");
        return;
    }
    const size_t length = static_cast<size_t>(end - start + 1);
    AsyncWebServerResponse* response = request->beginResponse("audio/wav", length,
        [stream, length](uint8_t* buffer, size_t max_len, size_t index) -> size_t {
            if (index >= length) return 0;
            size_t count = length - index;
            if (count > max_len) count = max_len;
            if (count > HTTP_STREAM_CHUNK_SIZE) count = HTTP_STREAM_CHUNK_SIZE;
            const size_t actual = stream->file.read(buffer, count);
            if (actual != count) LOGE("VoiceClips", "Clip stream read failed");
            return actual;
        });
    if (!response) {
        web_portal_send_json_error(request, 503, "Cannot start clip stream");
        return;
    }
    response->addHeader("Accept-Ranges", "bytes");
    response->addHeader("X-Content-Type-Options", "nosniff");
    if (partial) {
        char range[80];
        snprintf(range, sizeof(range), "bytes %llu-%llu/%llu",
                 static_cast<unsigned long long>(start), static_cast<unsigned long long>(end),
                 static_cast<unsigned long long>(size));
        response->setCode(206);
        response->addHeader("Content-Range", range);
    }
    if (request->hasParam("download"))
        response->addHeader("Content-Disposition", String("attachment; filename=\"") + id + ".wav\"");
    request->send(response);
}

void voice_clips_delete(AsyncWebServerRequest* request) {
    const String id = request->hasParam("id") ? request->getParam("id")->value() : "";
    if (!voice_clip_id_valid(id)) {
        web_portal_send_json_error(request, 400, "Invalid clip ID");
        return;
    }
    if (voice_clip_file_error(request, voice_clips_remove(id.c_str()))) return;
    request->send(200, "application/json", "{\"success\":true}");
}

const ComponentAction voice_clips_actions[] = {
    {"list", HTTP_GET, voice_clips_list_get, nullptr},
    {"status", HTTP_GET, voice_clips_status_get, nullptr},
    {"file", HTTP_GET, voice_clips_file_get, nullptr},
    {"delete", HTTP_DELETE, voice_clips_delete, nullptr},
};
}

static ComponentDef voice_clips_component = {
    .id = "voice-clips",
    .category = "audio",
    .display_name = "Voice clips",
    .nav_order = 50,
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = voice_clips_actions,
    .num_custom_actions = sizeof(voice_clips_actions) / sizeof(voice_clips_actions[0]),
    .fragment_id = "voice-clips",
    .portal_script = "/portal-voice-clips.js",
};
REGISTER_COMPONENT(voice_clips)
