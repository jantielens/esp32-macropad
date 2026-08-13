#include "audio.h"

#if HAS_AUDIO

#include <math.h>
#include "esp_memory_utils.h"
#include <esp_timer.h>
#include "audio_output_driver.h"
#include "device_telemetry.h"
#include "log_manager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#if HAS_SOUND_PLAYER
#include "sound_player.h"
#include "music_catalog.h"
#include "music_catalog_store.h"
#include "music_command.h"
#include "music_transport.h"
#include "tone_alert_overlay.h"
#if HAS_MUSIC_ANALYSIS
#include "music_analysis.h"
#endif
#endif

#define TAG "Audio"

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static bool audio_initialized = false;
static uint8_t current_volume = 70; // 0-100
static AudioOutputDriver* output_driver = nullptr;

// ---------------------------------------------------------------------------
// Async playback queue
// ---------------------------------------------------------------------------
#define AUDIO_PATTERN_MAX_LEN 128
#define AUDIO_QUEUE_DEPTH     2
#define MUSIC_WORK_QUEUE_DEPTH 4

struct AudioCommand {
    char pattern[AUDIO_PATTERN_MAX_LEN];
    uint8_t volume_override; // 0 = use current
    bool loop;               // true = repeat until stop
#if HAS_SOUND_PLAYER
    bool is_sound;           // true = play sound file (pattern holds filename)
    bool is_memory_sound;
    uint8_t* mp3;
    size_t mp3_size;
    AudioPlaybackGuard guard;
    uint32_t generation;
#endif
};

#if HAS_SOUND_PLAYER
enum MusicWorkKind : uint8_t { MUSIC_WORK_TRANSPORT, MUSIC_WORK_REFRESH };
struct MusicWorkCommand {
    MusicWorkKind kind;
    MusicCommand transport;
};
#endif

static QueueHandle_t audio_queue = NULL;
static QueueHandle_t music_work_queue = NULL;
static TaskHandle_t audio_task_handle = NULL;
// Cross-task flags: written by audio_enqueue()/audio_stop() (caller task),
// read/written by audio_task (FreeRTOS task).  volatile provides visibility;
// no mutex needed because the only race (g_playing read in audio_enqueue vs
// g_playing write in audio_task) is benign — a spurious g_stop_requested=true
// when nothing is playing is harmlessly cleared on the next queue receive.
static volatile bool g_stop_requested = false;
static volatile bool g_playing = false;

static void audio_command_dispose(AudioCommand* command) {
#if HAS_SOUND_PLAYER
    if (command->is_memory_sound && command->mp3) heap_caps_free(command->mp3);
#else
    (void)command;
#endif
}

#if HAS_SOUND_PLAYER
static bool g_file_backed_playing = false;
static AudioMusicInfo g_music_info = {AUDIO_MUSIC_UNAVAILABLE, 0, 0, {0}};
static portMUX_TYPE g_music_info_mux = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE g_music_storage_mux = portMUX_INITIALIZER_UNLOCKED;
static bool g_music_storage_mutating = false;
static bool g_music_catalog_dirty = false;
static StaticSemaphore_t g_music_catalog_refresh_sem_storage;
static SemaphoreHandle_t g_music_catalog_refresh_sem = nullptr;
static bool g_music_catalog_refresh_pending = false;

static bool music_storage_playback_claim() {
    portENTER_CRITICAL(&g_music_storage_mux);
    const bool available = !g_music_storage_mutating && !g_file_backed_playing;
    if (available) g_file_backed_playing = true;
    portEXIT_CRITICAL(&g_music_storage_mux);
    return available;
}

static void music_storage_playback_release() {
    portENTER_CRITICAL(&g_music_storage_mux);
    g_file_backed_playing = false;
    portEXIT_CRITICAL(&g_music_storage_mux);
}

static void music_tone_alert_transform(void* context, int16_t* frames, size_t frame_count) {
#if HAS_MUSIC_ANALYSIS
    music_analysis_process(frames, frame_count);
#endif
    tone_alert_overlay_mix((ToneAlertOverlay*)context, frames, frame_count, AUDIO_SAMPLE_RATE);
}

static void music_info_set(AudioMusicStatus status, const MusicCatalogSnapshot* catalog,
                           const MusicTransport& transport, const SoundPlayer* player = nullptr) {
    AudioMusicInfo info = {};
    info.status = status;
    info.count = catalog && catalog->available ? catalog->count : 0;
    info.index = transport.has_current_track() ? transport.track_index() + 1 : 0;
    if (catalog && transport.has_current_track() && transport.track_index() < catalog->count) {
        const uint8_t index = transport.track_index();
        strlcpy(info.file, catalog->paths[index], sizeof(info.file));
        info.metadata = catalog->metadata[index];
        if (info.metadata.duration_s) info.total_us = (uint64_t)info.metadata.duration_s * 1000000ULL;
    }
    if (player) {
        uint64_t ignored_total_us = 0;
        sound_player_get_timing(player, &ignored_total_us, &info.elapsed_us);
    }
    portENTER_CRITICAL(&g_music_info_mux);
    g_music_info = info;
    portEXIT_CRITICAL(&g_music_info_mux);
}

static void music_info_update_timing(const SoundPlayer* player) {
    uint64_t total_us = 0;
    uint64_t elapsed_us = 0;
    if (!player || !sound_player_get_timing(player, &total_us, &elapsed_us)) return;
    portENTER_CRITICAL(&g_music_info_mux);
    // The player deliberately skips full-file duration scans. Preserve the
    // catalog's fast Xing/VBRI/CBR duration whenever the player reports zero.
    const uint64_t effective_total_us = total_us ? total_us : g_music_info.total_us;
    if (g_music_info.total_us != effective_total_us ||
        g_music_info.elapsed_us / 1000000ULL != elapsed_us / 1000000ULL) {
        g_music_info.total_us = effective_total_us;
        g_music_info.elapsed_us = elapsed_us;
    }
    portEXIT_CRITICAL(&g_music_info_mux);
}
#endif

static constexpr int64_t kOutputBufferedUs =
    (int64_t)AUDIO_DMA_DESC_NUM * AUDIO_DMA_FRAME_NUM * 1000000 / AUDIO_SAMPLE_RATE;

bool audio_write_with_stats(AudioOutputDriver* driver, const int16_t* frames,
                            size_t frame_count, AudioStarvationStats* stats) {
    const int64_t before_write_us = esp_timer_get_time();
    if (stats->previous_write_complete_us != 0) {
        const int64_t gap_us = before_write_us - stats->previous_write_complete_us;
        if (gap_us > kOutputBufferedUs) {
            stats->event_count++;
            if (gap_us > stats->worst_gap_us) stats->worst_gap_us = gap_us;
        }
    }
    const bool ok = driver->write(frames, frame_count);
    stats->previous_write_complete_us = esp_timer_get_time();
    return ok;
}

void audio_log_starvation(const AudioStarvationStats& stats) {
    LOGI(TAG, "Output starvation: events=%u worst_gap_us=%lld buffered_us=%lld",
         stats.event_count, stats.worst_gap_us, kOutputBufferedUs);
}

// ---------------------------------------------------------------------------
// Tone generation — play one segment (freq Hz for duration_ms)
// ---------------------------------------------------------------------------
static void play_tone(uint16_t freq_hz, uint16_t duration_ms, AudioStarvationStats* stats) {
    if (!output_driver) return;

    uint32_t total_samples = (uint32_t)AUDIO_SAMPLE_RATE * duration_ms / 1000;
    if (total_samples == 0) return;

    static const size_t FRAMES_PER_CHUNK = 512;
    int16_t buf[FRAMES_PER_CHUNK * 2];

    if (freq_hz == 0) {
        // Silence: write zeros
        memset(buf, 0, sizeof(buf));
        uint32_t frames_done = 0;
        while (frames_done < total_samples) {
            size_t chunk = (total_samples - frames_done < FRAMES_PER_CHUNK)
                         ? (total_samples - frames_done) : FRAMES_PER_CHUNK;
            if (!audio_write_with_stats(output_driver, buf, chunk, stats)) return;
            frames_done += chunk;
        }
        return;
    }

    float phase_inc = 2.0f * M_PI * freq_hz / AUDIO_SAMPLE_RATE;
    float phase = 0.0f;
    const float amplitude = 32767.0f;
    const uint32_t fade_len = (total_samples > 400) ? 200 : total_samples / 4;

    uint32_t frames_done = 0;
    while (frames_done < total_samples) {
        size_t chunk = (total_samples - frames_done < FRAMES_PER_CHUNK)
                     ? (total_samples - frames_done) : FRAMES_PER_CHUNK;

        for (size_t i = 0; i < chunk; i++) {
            uint32_t g = frames_done + i;
            float env = 1.0f;
            if (g < fade_len) {
                env = (float)g / fade_len;
            } else if (g > total_samples - fade_len) {
                env = (float)(total_samples - g) / fade_len;
            }
            int16_t sample = (int16_t)(sinf(phase) * amplitude * env);
            buf[i * 2]     = sample;
            buf[i * 2 + 1] = sample;
            phase += phase_inc;
            if (phase >= 2.0f * M_PI) phase -= 2.0f * M_PI;
        }

        if (!audio_write_with_stats(output_driver, buf, chunk, stats)) return;
        frames_done += chunk;
    }
}

// ---------------------------------------------------------------------------
// Beep pattern parser
//   Space-delimited: "freq:dur" for tones, bare "dur" for silence gaps
//   e.g. "1000:200 100 1000:200" = beep, 100ms gap, beep
// ---------------------------------------------------------------------------
static void play_pattern(const char* pattern, AudioStarvationStats* stats) {
    if (!pattern || !pattern[0]) {
        play_tone(1000, 200, stats); // default beep
        return;
    }

    char buf[AUDIO_PATTERN_MAX_LEN];
    strlcpy(buf, pattern, sizeof(buf));

    char* saveptr = NULL;
    char* tok = strtok_r(buf, " ", &saveptr);
    while (tok) {
        if (g_stop_requested) return;
        char* colon = strchr(tok, ':');
        if (colon) {
            *colon = '\0';
            uint16_t freq = (uint16_t)atoi(tok);
            uint16_t dur  = (uint16_t)atoi(colon + 1);
            if (dur > 0 && dur <= 10000) {
                play_tone(freq, dur, stats);
            }
        } else {
            uint16_t dur = (uint16_t)atoi(tok);
            if (dur > 0 && dur <= 10000) {
                play_tone(0, dur, stats); // silence gap
            }
        }
        tok = strtok_r(NULL, " ", &saveptr);
    }
}

// ---------------------------------------------------------------------------
// Audio task — processes queued beep commands
// PA stays on permanently after first use (avoids settle-time beep clipping).
// ---------------------------------------------------------------------------
static void audio_task(void* param) {
    AudioCommand cmd;
#if HAS_SOUND_PLAYER
    MusicCatalog music_catalog;
    MusicTransport music_transport;
    SoundPlayer* music_player = nullptr;
    ToneAlertOverlay music_tone_alert = {};
    auto refresh_music_catalog = [&](bool force) {
        bool dirty = false;
        portENTER_CRITICAL(&g_music_storage_mux);
        dirty = g_music_catalog_dirty;
        g_music_catalog_dirty = false;
        portEXIT_CRITICAL(&g_music_storage_mux);
        if (!force && !dirty) return;
        MusicCatalogSnapshot* build_slot = music_catalog_store_begin_build();
        if (!build_slot) {
            LOGW(TAG, "Music catalog refresh skipped: store busy");
            if (dirty) {
                portENTER_CRITICAL(&g_music_storage_mux);
                g_music_catalog_dirty = true;
                portEXIT_CRITICAL(&g_music_storage_mux);
            }
            return;
        }
        const bool discovered = music_catalog_discover(&music_catalog, build_slot);
        (void)discovered;
        music_catalog_store_publish(music_catalog.result());
    };
    refresh_music_catalog(true);
    const MusicCatalogSnapshot* startup_catalog = music_catalog_store_active_for_audio();
    LOGI(TAG, "Music catalog startup scan: available=%d count=%u",
            startup_catalog && startup_catalog->available,
            startup_catalog ? startup_catalog->count : 0);
        LOGI(TAG, "Music catalog scan: audio stack free=%u bytes",
         (unsigned)uxTaskGetStackHighWaterMark(nullptr));
        music_info_set(startup_catalog && startup_catalog->available
                   ? (startup_catalog->count ? AUDIO_MUSIC_STOPPED : AUDIO_MUSIC_EMPTY)
                       : AUDIO_MUSIC_UNAVAILABLE,
               startup_catalog, music_transport);

    auto close_music = [&]() {
        tone_alert_overlay_stop(&music_tone_alert);
#if HAS_MUSIC_ANALYSIS
        music_analysis_set_playing(false);
#endif
        if (music_player) {
            sound_player_close(music_player);
            music_player = nullptr;
            music_storage_playback_release();
        }
    };
    auto open_music_track = [&](const char* path) {
        if (!music_storage_playback_claim()) return false;
        music_player = sound_player_begin_path(output_driver, path,
                                               music_tone_alert_transform, &music_tone_alert);
    #if HAS_MUSIC_ANALYSIS
        if (music_player) music_analysis_set_playing(true);
    #endif
        if (!music_player) music_storage_playback_release();
        return music_player != nullptr;
    };
    auto apply_music = [&](MusicCommand command) {
        portENTER_CRITICAL(&g_music_storage_mux);
        const bool mutating = g_music_storage_mutating;
        portEXIT_CRITICAL(&g_music_storage_mux);
        if (mutating) return;
        const MusicTransportCommand transport_command =
            command == MUSIC_COMMAND_PLAY_PAUSE ? MUSIC_TRANSPORT_PLAY_PAUSE :
            command == MUSIC_COMMAND_NEXT ? MUSIC_TRANSPORT_NEXT :
            command == MUSIC_COMMAND_PREVIOUS ? MUSIC_TRANSPORT_PREVIOUS : MUSIC_TRANSPORT_STOP;
        if (music_transport.state() == MUSIC_TRANSPORT_STOPPED) {
            refresh_music_catalog(false);
        }
        const MusicCatalogSnapshot* active_catalog = music_catalog_store_active_for_audio();
        if (!active_catalog || !active_catalog->available ||
            (!active_catalog->count && transport_command == MUSIC_TRANSPORT_PLAY_PAUSE)) {
            music_info_set(active_catalog && active_catalog->available
                               ? AUDIO_MUSIC_EMPTY : AUDIO_MUSIC_UNAVAILABLE,
                           active_catalog, music_transport);
            return;
        }
        const MusicTransportResult result = music_transport.apply(transport_command, active_catalog->count);
        if (result.effect == MUSIC_TRANSPORT_CLOSE_TRACK) close_music();
        if ((result.effect == MUSIC_TRANSPORT_OPEN_TRACK ||
             (result.effect == MUSIC_TRANSPORT_CLOSE_TRACK &&
              music_transport.state() == MUSIC_TRANSPORT_PLAYING)) &&
            music_transport.has_current_track()) {
            if (!open_music_track(active_catalog->paths[result.track_index])) {
                music_transport.apply(MUSIC_TRANSPORT_FAILURE, active_catalog->count);
            }
        }
        const AudioMusicStatus status = music_transport.state() == MUSIC_TRANSPORT_PLAYING ? AUDIO_MUSIC_PLAYING :
            music_transport.state() == MUSIC_TRANSPORT_PAUSED ? AUDIO_MUSIC_PAUSED : AUDIO_MUSIC_STOPPED;
        music_info_set(status, active_catalog, music_transport, music_player);
    };
#endif

    for (;;) {
        const TickType_t wait =
#if HAS_SOUND_PLAYER
            music_transport.state() == MUSIC_TRANSPORT_PLAYING ? 0 : pdMS_TO_TICKS(250);
#else
            portMAX_DELAY;
#endif
        MusicWorkCommand music_work = {};
        if (xQueueReceive(music_work_queue, &music_work, 0) == pdTRUE) {
            if (music_work.kind == MUSIC_WORK_TRANSPORT) {
                apply_music(music_work.transport);
            } else {
                refresh_music_catalog(true);
                portENTER_CRITICAL(&g_music_storage_mux);
                g_music_catalog_refresh_pending = false;
                portEXIT_CRITICAL(&g_music_storage_mux);
                xSemaphoreGive(g_music_catalog_refresh_sem);
            }
            continue;
        }
        if (xQueueReceive(audio_queue, &cmd, wait) == pdTRUE) {
#if HAS_SOUND_PLAYER
            if (cmd.is_memory_sound && cmd.guard && !cmd.guard(cmd.generation)) {
                audio_command_dispose(&cmd);
                continue;
            }
            if (music_player && music_transport.state() == MUSIC_TRANSPORT_PLAYING &&
                !cmd.is_sound && !cmd.is_memory_sound) {
                float tone_gain = 1.0f;
                if (cmd.volume_override > 0 && cmd.volume_override <= 100) {
                    tone_gain = current_volume == 0 ? 0.0f :
                        (float)cmd.volume_override / current_volume;
                }
                if (!tone_alert_overlay_start(&music_tone_alert, cmd.pattern, cmd.loop,
                                              AUDIO_SAMPLE_RATE, tone_gain)) {
                    LOGW(TAG, "Tone alert pattern rejected while Music is playing");
                }
                continue;
            }
            const bool preserve_paused_music = music_player &&
                music_transport.state() == MUSIC_TRANSPORT_PAUSED && !cmd.is_sound;
            if (music_player && !preserve_paused_music) {
                close_music();
                const MusicCatalogSnapshot* active_catalog = music_catalog_store_active_for_audio();
                if (active_catalog) {
                    music_transport.apply(MUSIC_TRANSPORT_STOP, active_catalog->count);
                    music_info_set(AUDIO_MUSIC_STOPPED, active_catalog, music_transport);
                }
            }
#endif
            g_stop_requested = false;
            g_playing = true;

            uint8_t play_vol = current_volume;
            if (cmd.volume_override > 0 && cmd.volume_override <= 100) {
                play_vol = cmd.volume_override;
            }
            LOGD(TAG, "Play: vol=%u%% (override=%u, device=%u) loop=%d", play_vol, cmd.volume_override, current_volume, cmd.loop);
            output_driver->setVolume(play_vol);

#if HAS_SOUND_PLAYER
            if (cmd.is_memory_sound) {
                sound_player_play_memory(output_driver, cmd.mp3, cmd.mp3_size, &g_stop_requested,
                                         cmd.guard, cmd.generation);
                audio_command_dispose(&cmd);
            } else if (cmd.is_sound) {
                if (music_storage_playback_claim()) {
                    sound_player_play(output_driver, cmd.pattern, &g_stop_requested);
                    music_storage_playback_release();
                } else {
                    LOGW(TAG, "MP3 alert skipped while Music storage is busy");
                }
            } else
#endif
            if (cmd.loop) {
                AudioStarvationStats stats = {};
                while (!g_stop_requested) {
                    play_pattern(cmd.pattern, &stats);
                }
                audio_log_starvation(stats);
            } else {
                AudioStarvationStats stats = {};
                play_pattern(cmd.pattern, &stats);
                audio_log_starvation(stats);
            }

            // Restore device volume if overridden
            if (cmd.volume_override > 0 && cmd.volume_override <= 100) {
                output_driver->setVolume(current_volume);
            }
            g_playing = false;
        }
#if HAS_SOUND_PLAYER
    if (music_transport.state() == MUSIC_TRANSPORT_STOPPED) {
            refresh_music_catalog(false);
    }
        if (music_player && music_transport.state() == MUSIC_TRANSPORT_PLAYING) {
            if (g_stop_requested) {
                tone_alert_overlay_stop(&music_tone_alert);
                g_stop_requested = false;
            }
            const SoundPlayerStepResult step = sound_player_step(music_player);
            if (step != SOUND_PLAYER_STEP_PLAYING) {
                close_music();
                const MusicCatalogSnapshot* before_transition = music_catalog_store_active_for_audio();
                const MusicTransportResult result = music_transport.apply(
                    step == SOUND_PLAYER_STEP_COMPLETE ? MUSIC_TRANSPORT_COMPLETE : MUSIC_TRANSPORT_FAILURE,
                    before_transition ? before_transition->count : 0);
                if (result.effect == MUSIC_TRANSPORT_CLOSE_TRACK &&
                    music_transport.state() == MUSIC_TRANSPORT_PLAYING) {
                    const MusicCatalogSnapshot* active_catalog = music_catalog_store_active_for_audio();
                    if (!active_catalog || !open_music_track(active_catalog->paths[result.track_index])) {
                        music_transport.apply(MUSIC_TRANSPORT_FAILURE,
                                              active_catalog ? active_catalog->count : 0);
                    }
                }
                const MusicCatalogSnapshot* active_catalog = music_catalog_store_active_for_audio();
                music_info_set(step == SOUND_PLAYER_STEP_ERROR ? AUDIO_MUSIC_ERROR :
                    (music_transport.state() == MUSIC_TRANSPORT_PLAYING ? AUDIO_MUSIC_PLAYING : AUDIO_MUSIC_STOPPED),
                    active_catalog, music_transport, music_player);
            } else {
                music_info_update_timing(music_player);
            }
        } else if (music_player) {
#if HAS_MUSIC_ANALYSIS
            // No PCM is produced while paused, so clear the last visualizer
            // frame instead of leaving stale levels on the display.
            music_analysis_set_playing(false);
#endif
            const MusicCatalogSnapshot* active_catalog = music_catalog_store_active_for_audio();
            music_info_set(AUDIO_MUSIC_PAUSED, active_catalog,
                           music_transport, music_player);
        }
#endif
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void audio_init(uint8_t initial_volume) {
    LOGI(TAG, "Initializing audio output");

#if HAS_MUSIC_ANALYSIS
    music_analysis_init();
#endif

    current_volume = (initial_volume > 100) ? 100 : initial_volume;
    output_driver = audio_output_driver_create();
    if (!output_driver) {
        LOGE(TAG, "No audio output driver");
        return;
    }
    output_driver->setMuted(false);
    if (!output_driver->begin(AUDIO_SAMPLE_RATE)) return;

    output_driver->setVolume(current_volume);

    // Create command queue and audio task. The stack remains internal because
    // sound playback reads LittleFS while the flash cache is disabled.
    // Priority 5: above LVGL (4) to avoid I2S DMA underruns during heavy rendering
    // Boards can move minimp3's 16 KB scratch workspace to PSRAM and select a
    // smaller internal stack with AUDIO_TASK_STACK_SIZE.
    audio_queue = xQueueCreate(AUDIO_QUEUE_DEPTH, sizeof(AudioCommand));
    if (!audio_queue) {
        LOGE(TAG, "Failed to create audio queue");
        return;
    }

#if HAS_SOUND_PLAYER
    music_work_queue = xQueueCreate(MUSIC_WORK_QUEUE_DEPTH, sizeof(MusicWorkCommand));
    if (!music_work_queue) {
        LOGE(TAG, "Failed to create Music work queue");
        vQueueDelete(audio_queue);
        audio_queue = NULL;
        return;
    }
    if (!music_catalog_store_init()) {
        LOGE(TAG, "Failed to allocate Music catalog in PSRAM");
    }
    g_music_catalog_refresh_sem = xSemaphoreCreateBinaryStatic(&g_music_catalog_refresh_sem_storage);
    if (!g_music_catalog_refresh_sem) {
        LOGE(TAG, "Failed to create Music catalog refresh semaphore");
        vQueueDelete(audio_queue);
        audio_queue = NULL;
        return;
    }
#endif

    BaseType_t task_result = xTaskCreatePinnedToCoreWithCaps(
        audio_task, "audio", AUDIO_TASK_STACK_SIZE, NULL, 5,
        &audio_task_handle, 1,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (task_result != pdPASS) {
        LOGE(TAG, "Failed to create audio task with internal stack");
        vQueueDelete(audio_queue);
        audio_queue = NULL;
        return;
    }

    uint8_t* stack_start = pxTaskGetStackStart(audio_task_handle);
    uint8_t* stack_end = stack_start + AUDIO_TASK_STACK_SIZE - 1;
    if (!esp_ptr_internal(stack_start) || !esp_ptr_internal(stack_end)) {
        LOGE(TAG, "Audio task stack is not internal: %p-%p", stack_start, stack_end);
        vTaskDelete(audio_task_handle);
        audio_task_handle = NULL;
        vQueueDelete(audio_queue);
        audio_queue = NULL;
        return;
    }

    audio_initialized = true;
    device_telemetry_log_memory_snapshot("audio");
    LOGI(TAG, "Audio ready (volume=%u%%, PA always-on)", current_volume);
}

void audio_set_volume(uint8_t vol_0_100) {
    if (vol_0_100 > 100) vol_0_100 = 100;
    current_volume = vol_0_100;
    if (audio_initialized) {
        output_driver->setVolume(current_volume);
    }
    LOGI(TAG, "Volume: %u%%", current_volume);
}

uint8_t audio_get_volume() {
    return current_volume;
}

static void audio_enqueue(const char* pattern, uint8_t volume_override, bool loop) {
    if (!audio_initialized) {
        LOGW(TAG, "Audio not initialized");
        return;
    }

    // If starting a new command, stop any current loop first
    if (g_playing) {
        g_stop_requested = true;
    }

    // Flush queue
    AudioCommand discard;
    while (xQueueReceive(audio_queue, &discard, 0) == pdTRUE) audio_command_dispose(&discard);

    AudioCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    if (pattern && pattern[0]) {
        strlcpy(cmd.pattern, pattern, AUDIO_PATTERN_MAX_LEN);
    }
    cmd.volume_override = volume_override;
    cmd.loop = loop;

    xQueueSend(audio_queue, &cmd, portMAX_DELAY);
}

void audio_beep(const char* pattern, uint8_t volume_override) {
    audio_enqueue(pattern, volume_override, false);
}

void audio_play_loop(const char* pattern, uint8_t volume_override) {
    audio_enqueue(pattern, volume_override, true);
}

void audio_stop() {
    if (!audio_initialized) return;
    g_stop_requested = true;
    // Flush queued commands
    AudioCommand discard;
    while (xQueueReceive(audio_queue, &discard, 0) == pdTRUE) audio_command_dispose(&discard);
    LOGD(TAG, "Stop requested");
}

bool audio_is_playing() {
    return g_playing;
}

#if HAS_SOUND_PLAYER
AudioMusicSubmitResult audio_music_command(MusicCommand command) {
    if (!audio_initialized || !music_work_queue) return AUDIO_MUSIC_SUBMIT_UNAVAILABLE;
    portENTER_CRITICAL(&g_music_storage_mux);
    const bool mutating = g_music_storage_mutating;
    portEXIT_CRITICAL(&g_music_storage_mux);
    if (mutating) return AUDIO_MUSIC_SUBMIT_BUSY;
    MusicWorkCommand work = {MUSIC_WORK_TRANSPORT, command};
    return xQueueSend(music_work_queue, &work, 0) == pdTRUE
        ? AUDIO_MUSIC_SUBMIT_QUEUED : AUDIO_MUSIC_SUBMIT_BUSY;
}

void audio_get_music_info(AudioMusicInfo* out) {
    if (!out) return;
    portENTER_CRITICAL(&g_music_info_mux);
    *out = g_music_info;
    portEXIT_CRITICAL(&g_music_info_mux);
}

bool audio_get_music_catalog_snapshot(MusicCatalogSnapshot* out) {
    return music_catalog_store_copy(out, nullptr);
}

bool audio_get_music_catalog_status(MusicCatalogStatus* out) {
    return music_catalog_store_status(out);
}

bool audio_get_music_catalog_count(uint8_t* out_count) {
    if (!out_count) return false;
    MusicCatalogStatus status = {};
    if (!music_catalog_store_status(&status)) return false;
    *out_count = status.available ? status.count : 0;
    return status.available;
}

bool audio_music_refresh_catalog(uint32_t timeout_ms) {
    if (!audio_initialized || !g_music_catalog_refresh_sem) return false;
    xSemaphoreTake(g_music_catalog_refresh_sem, 0);
    portENTER_CRITICAL(&g_music_storage_mux);
    if (g_music_catalog_refresh_pending) {
        portEXIT_CRITICAL(&g_music_storage_mux);
        return false;
    }
    g_music_catalog_refresh_pending = true;
    portEXIT_CRITICAL(&g_music_storage_mux);

    MusicWorkCommand command = {MUSIC_WORK_REFRESH, MUSIC_COMMAND_STOP};
    if (xQueueSend(music_work_queue, &command, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        portENTER_CRITICAL(&g_music_storage_mux);
        g_music_catalog_refresh_pending = false;
        portEXIT_CRITICAL(&g_music_storage_mux);
        return false;
    }
    return xSemaphoreTake(g_music_catalog_refresh_sem, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

bool audio_music_storage_mutation_begin() {
    if (!audio_initialized) return false;
    portENTER_CRITICAL(&g_music_storage_mux);
    if (g_music_storage_mutating || g_file_backed_playing) {
        portEXIT_CRITICAL(&g_music_storage_mux);
        return false;
    }
    portENTER_CRITICAL(&g_music_info_mux);
    const AudioMusicStatus status = g_music_info.status;
    portEXIT_CRITICAL(&g_music_info_mux);
    if (status == AUDIO_MUSIC_PLAYING || status == AUDIO_MUSIC_PAUSED) {
        portEXIT_CRITICAL(&g_music_storage_mux);
        return false;
    }
    g_music_storage_mutating = true;
    portEXIT_CRITICAL(&g_music_storage_mux);
    return true;
}

void audio_music_storage_mutation_end(bool catalog_changed) {
    portENTER_CRITICAL(&g_music_storage_mux);
    if (catalog_changed) g_music_catalog_dirty = true;
    g_music_storage_mutating = false;
    portEXIT_CRITICAL(&g_music_storage_mux);
}

void audio_play_sound(const char* filename, uint8_t volume_override) {
    if (!audio_initialized) {
        LOGW(TAG, "Audio not initialized");
        return;
    }
    if (!filename || !filename[0]) {
        LOGW(TAG, "Empty sound filename");
        return;
    }
    portENTER_CRITICAL(&g_music_storage_mux);
    const bool mutating = g_music_storage_mutating;
    portEXIT_CRITICAL(&g_music_storage_mux);
    if (mutating) return;
    // Note: don't check sound_store_exists() here — it does flash I/O and the
    // caller may be the LVGL task whose stack is in PSRAM (crashes on ESP32-P4).
    // sound_player_play() handles file-not-found gracefully.

    // Stop any current playback
    if (g_playing) {
        g_stop_requested = true;
    }
    AudioCommand discard;
    while (xQueueReceive(audio_queue, &discard, 0) == pdTRUE) audio_command_dispose(&discard);

    AudioCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    strlcpy(cmd.pattern, filename, AUDIO_PATTERN_MAX_LEN);
    cmd.volume_override = volume_override;
    cmd.loop = false;
    cmd.is_sound = true;

    xQueueSend(audio_queue, &cmd, portMAX_DELAY);
}

void audio_play_mp3_buffer(uint8_t* mp3, size_t mp3_size, uint8_t volume_override,
                           AudioPlaybackGuard guard, uint32_t generation) {
    if (!mp3 || !mp3_size || !audio_initialized) {
        if (mp3) heap_caps_free(mp3);
        return;
    }
    audio_stop();
    AudioCommand cmd = {};
    cmd.volume_override = volume_override;
    cmd.is_memory_sound = true;
    cmd.mp3 = mp3;
    cmd.mp3_size = mp3_size;
    cmd.guard = guard;
    cmd.generation = generation;
    if (xQueueSend(audio_queue, &cmd, 0) != pdTRUE) audio_command_dispose(&cmd);
}
#endif // HAS_SOUND_PLAYER

#endif // HAS_AUDIO
