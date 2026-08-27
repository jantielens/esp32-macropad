#include "music_binding.h"

#include "board_config.h"

#if HAS_DISPLAY && HAS_AUDIO && HAS_SOUND_PLAYER

#include "audio.h"
#include "binding_template.h"
#include "log_manager.h"
#if HAS_MUSIC_ANALYSIS
#include "music_analysis.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

const char* const kMusicKeys[] = {
    "file", "file_name", "title", "artist", "album", "track", "index", "count",
    "elapsed_s", "total_s", "status",
#if HAS_MUSIC_ANALYSIS
    "analysis.rms", "analysis.peak", "analysis.band.0", "analysis.band.1",
    "analysis.band.2", "analysis.band.3", "analysis.band.4", "analysis.band.5",
    "analysis.band.6", "analysis.band.7",
#endif
};

const char* find_music_key(const char* params) {
    if (!params) return nullptr;
    for (const char* key : kMusicKeys) {
        if (strcmp(params, key) == 0) return key;
    }
    return nullptr;
}

uint8_t music_binding_key_count() {
    return sizeof(kMusicKeys) / sizeof(kMusicKeys[0]);
}

const char* music_binding_key_at(uint8_t index) {
    return index < music_binding_key_count() ? kMusicKeys[index] : nullptr;
}

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

BindingResolverStatus music_binding_resolve(const char* params, char* out, size_t out_len) {
    if (!find_music_key(params)) return BINDING_RESOLVER_UNKNOWN;
    if (!out || out_len == 0) return BINDING_RESOLVER_UNAVAILABLE;
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
#if HAS_MUSIC_ANALYSIS
    } else if (strcmp(params, "analysis.rms") == 0 ||
               strcmp(params, "analysis.peak") == 0 ||
               strncmp(params, "analysis.band.", 14) == 0) {
        MusicAnalysisSnapshot analysis = {};
        music_analysis_get_snapshot(&analysis);
        if (strcmp(params, "analysis.rms") == 0) {
            snprintf(out, out_len, "%u", analysis.rms);
        } else if (strcmp(params, "analysis.peak") == 0) {
            snprintf(out, out_len, "%u", analysis.peak);
        } else {
            const int band = atoi(params + 14);
            if (band < 0 || band >= 8) return BINDING_RESOLVER_UNKNOWN;
            snprintf(out, out_len, "%u", analysis.bands[band]);
        }
#endif
    } else {
        return BINDING_RESOLVER_UNKNOWN;
    }
    return BINDING_RESOLVER_RESOLVED;
}

void music_binding_collect(const char* params, void* user_data) {
    (void)params;
    (void)user_data;
}

} // namespace

void music_binding_init() {
    if (!binding_template_register("music", music_binding_resolve, music_binding_collect,
                                   {1, 1, 1, -1, BINDING_VALIDATION_STANDARD, false,
                                    music_binding_key_count, music_binding_key_at})) {
        LOGE("MusicBind", "Failed to register music binding scheme");
        return;
    }
}

#else

void music_binding_init() {}

#endif