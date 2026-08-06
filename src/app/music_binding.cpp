#include "music_binding.h"

#include "board_config.h"

#if HAS_DISPLAY && HAS_AUDIO && HAS_SOUND_PLAYER

#include "audio.h"
#include "binding_template.h"
#include "log_manager.h"

#include <stdio.h>
#include <string.h>

#if HAS_MCP
#include <ArduinoJson.h>
#endif

namespace {

const char* const kMusicKeys[] = {
    "file", "file_name", "title", "artist", "album", "track", "index", "count",
    "elapsed_s", "total_s", "status",
};

const char* status_text(AudioMusicStatus status) {
    switch (status) {
        case AUDIO_MUSIC_PLAYING: return "playing";
        case AUDIO_MUSIC_PAUSED: return "paused";
        case AUDIO_MUSIC_STOPPED: return "stopped";
        case AUDIO_MUSIC_EMPTY: return "empty";
        case AUDIO_MUSIC_UNAVAILABLE: return "unavailable";
        case AUDIO_MUSIC_ERROR: return "error";
    }
    return "unavailable";
}

bool music_binding_resolve(const char* params, char* out, size_t out_len) {
    if (!params || !out || out_len == 0) return false;
    AudioMusicInfo info = {};
    audio_get_music_info(&info);
    if (strcmp(params, "file") == 0) {
        strlcpy(out, info.file[0] ? info.file : "---", out_len);
    } else if (strcmp(params, "file_name") == 0) {
        const char* filename = info.file[0] ? strrchr(info.file, '/') : nullptr;
        strlcpy(out, filename ? filename + 1 : "---", out_len);
    } else if (strcmp(params, "title") == 0) {
        strlcpy(out, info.metadata.title[0] ? info.metadata.title : "---", out_len);
    } else if (strcmp(params, "artist") == 0) {
        strlcpy(out, info.metadata.artist[0] ? info.metadata.artist : "---", out_len);
    } else if (strcmp(params, "album") == 0) {
        strlcpy(out, info.metadata.album[0] ? info.metadata.album : "---", out_len);
    } else if (strcmp(params, "track") == 0) {
        strlcpy(out, info.metadata.track[0] ? info.metadata.track : "---", out_len);
    } else if (strcmp(params, "index") == 0) {
        snprintf(out, out_len, "%u", info.index);
    } else if (strcmp(params, "count") == 0) {
        snprintf(out, out_len, "%d", info.status == AUDIO_MUSIC_UNAVAILABLE ? -1 : info.count);
    } else if (strcmp(params, "elapsed_s") == 0) {
        snprintf(out, out_len, "%llu", (unsigned long long)(info.elapsed_us / 1000000ULL));
    } else if (strcmp(params, "total_s") == 0) {
        if (info.total_us == 0) strlcpy(out, "-1", out_len);
        else snprintf(out, out_len, "%llu", (unsigned long long)(info.total_us / 1000000ULL));
    } else if (strcmp(params, "status") == 0) {
        strlcpy(out, status_text(info.status), out_len);
    } else {
        return false;
    }
    return true;
}

void music_binding_collect(const char* params, void* user_data) {
    (void)params;
    (void)user_data;
}

const char* music_binding_validate(const char* params) {
    if (!params || !params[0]) return "music key is required";
    for (const char* key : kMusicKeys) {
        if (strcmp(params, key) == 0) return nullptr;
    }
    return "music key must be file, file_name, title, artist, album, track, index, count, elapsed_s, total_s, or status";
}

#if HAS_MCP
void music_binding_describe(void* out_json) {
    JsonObject& out = *static_cast<JsonObject*>(out_json);
    out["syntax"] = "[music:file|file_name|title|artist|album|track|index|count|elapsed_s|total_s|status]";
    out["example"] = "[music:status]";
    out["keys"] = "file, file_name, title, artist, album, track, index, count, elapsed_s, total_s, status";
    out["read_only"] = true;
}
#endif

} // namespace

void music_binding_init() {
    if (!binding_template_register("music", music_binding_resolve, music_binding_collect)) {
        LOGE("MusicBind", "Failed to register music binding scheme");
        return;
    }
#if HAS_MCP
    binding_template_set_scheme_describe("music", music_binding_describe);
    binding_template_set_scheme_validate("music", music_binding_validate);
#endif
}

#else

void music_binding_init() {}

#endif