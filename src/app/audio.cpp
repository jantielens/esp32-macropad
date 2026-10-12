#include "audio.h"
#include "ota_activity.h"
#include "tone_alert_overlay.h"
#include <atomic>

#if HAS_AUDIO

#include <math.h>
#include "esp_memory_utils.h"
#include <esp_timer.h>
#include "audio_output_driver.h"
#include "device_telemetry.h"
#include "log_manager.h"
#include "rtos_task_utils.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#if HAS_SOUND_PLAYER
#include "sound_player.h"
#include "sound_store.h"
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
static std::atomic<uint8_t> current_volume{70};
static AudioOutputDriver* output_driver = nullptr;

// ---------------------------------------------------------------------------
// Async playback queue
// ---------------------------------------------------------------------------
#define AUDIO_PATTERN_MAX_LEN 128
#define AUDIO_QUEUE_DEPTH     8
#define MUSIC_WORK_QUEUE_DEPTH 4

enum AudioCommandKind : uint8_t { AUDIO_COMMAND_PLAY, AUDIO_COMMAND_SESSION, AUDIO_COMMAND_STOP_OWNED };

struct AudioCommand {
    AudioCommandKind kind;
    char pattern[AUDIO_PATTERN_MAX_LEN];
    uint8_t volume_override; // 0 = use current
    bool loop;               // true = repeat until stop
    AudioPlaybackGuard guard;
    uint32_t generation;
    uint32_t ota_epoch;
    uint32_t stop_epoch;
    bool alarm;
    AudioPlaybackFinished finished;
#if HAS_SOUND_PLAYER
    bool is_sound;           // true = play sound file (pattern holds filename)
    bool is_memory_sound;
    uint8_t* mp3;
    size_t mp3_size;
#endif
};

#if HAS_SOUND_PLAYER
enum MusicWorkKind : uint8_t { MUSIC_WORK_TRANSPORT, MUSIC_WORK_REFRESH };
struct MusicWorkCommand {
    MusicWorkKind kind;
    MusicCommand transport;
    AudioPlaybackGuard guard = nullptr;
    uint32_t generation = 0;
    uint32_t ota_epoch = 0;
    uint32_t stop_epoch = 0;
};
#endif

static QueueHandle_t audio_queue = NULL;
#if HAS_SOUND_PLAYER
static QueueHandle_t music_work_queue = NULL;
#endif
static std::atomic<TaskHandle_t> audio_task_handle{nullptr};
static std::atomic<uint32_t> g_alert_stop_epoch{0};
static std::atomic<uint32_t> g_all_stop_epoch{0};
static std::atomic<bool> g_playing{false};
static AudioPlaybackGuard g_playback_guard = nullptr;
static uint32_t g_playback_generation = 0;
static ToneAlertOverlay g_feedback = {};
static ToneAlertOverlay g_pending_feedback = {};
static bool g_feedback_pending = false;
static portMUX_TYPE g_feedback_mux = portMUX_INITIALIZER_UNLOCKED;

static void audio_command_dispose(AudioCommand* command) {
    if (command->finished) command->finished(command->generation);
#if HAS_SOUND_PLAYER
    if (command->is_memory_sound && command->mp3) heap_caps_free(command->mp3);
#else
    (void)command;
#endif
}

#if HAS_SOUND_PLAYER
static bool g_file_backed_playing = false;
static AudioMusicInfo g_music_info = {AUDIO_MUSIC_UNAVAILABLE, 0, 0, 0, 0, {}, {}};
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

static void music_pcm_transform(void*, int16_t* frames, size_t frame_count) {
#if HAS_MUSIC_ANALYSIS
    music_analysis_process(frames, frame_count);
#else
    (void)frames;
    (void)frame_count;
#endif
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
    if (ota_activity_is_active() || (g_playing && g_playback_guard && !g_playback_guard(g_playback_generation))) {
        return false;
    }
    const int64_t before_write_us = esp_timer_get_time();
    if (stats->previous_write_complete_us != 0) {
        const int64_t gap_us = before_write_us - stats->previous_write_complete_us;
        if (gap_us > kOutputBufferedUs) {
            stats->event_count++;
            if (gap_us > stats->worst_gap_us) stats->worst_gap_us = gap_us;
        }
    }
    int16_t mixed[512 * 2];
    bool ok = true;
    for (size_t offset = 0; offset < frame_count && ok; offset += 512) {
        const size_t count = frame_count - offset < 512 ? frame_count - offset : 512;
        for (size_t sample = 0; sample < count * 2; ++sample)
            mixed[sample] = (int16_t)((int32_t)frames[offset * 2 + sample] * 3 / 4);
        tone_alert_overlay_mix(&g_feedback, mixed, count, AUDIO_SAMPLE_RATE);
        if (ota_activity_is_active()) return false;
        ok = driver->write(mixed, count);
    }
    stats->previous_write_complete_us = esp_timer_get_time();
    return ok;
}

void audio_log_starvation(const AudioStarvationStats& stats) {
    if (!stats.event_count) return;
    LOGW(TAG, "Output starvation: events=%u worst_gap_us=%lld buffered_us=%lld",
         stats.event_count, stats.worst_gap_us, kOutputBufferedUs);
}

// ---------------------------------------------------------------------------
// Audio worker
// ---------------------------------------------------------------------------
static void audio_task(void* param) {
    (void)param;
    AudioCommand active = {};
    ToneAlertOverlay primary_tone = {};
    AudioStarvationStats tone_stats = {};
    AudioPlaybackGuard session_guard = nullptr;
    uint32_t session_generation = 0;
    uint32_t alert_stop_epoch = g_alert_stop_epoch.load();
    uint32_t all_stop_epoch = g_all_stop_epoch.load();
    uint8_t output_volume = 255;
#if HAS_SOUND_PLAYER
    SoundPlayer* alert_player = nullptr;
#endif
    auto close_alert = [&]() {
#if HAS_SOUND_PLAYER
        if (alert_player) {
            sound_player_close(alert_player);
            alert_player = nullptr;
            if (active.is_sound) music_storage_playback_release();
        }
#endif
        audio_command_dispose(&active);
        active = {};
        tone_alert_overlay_stop(&primary_tone);
        audio_log_starvation(tone_stats);
        tone_stats = {};
        g_playing.store(false);
        g_playback_guard = nullptr;
    };
#if HAS_SOUND_PLAYER
    MusicCatalog music_catalog;
    MusicTransport music_transport;
    SoundPlayer* music_player = nullptr;
    char remembered_path[MUSIC_PATH_MAX_LEN] = {};
    AudioPlaybackGuard music_guard = nullptr;
    uint32_t music_generation = 0;
    auto refresh_music_catalog = [&](bool force) {
        if (ota_activity_is_active()) return;
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
                                               music_pcm_transform);
    #if HAS_MUSIC_ANALYSIS
        if (music_player) music_analysis_set_playing(true);
    #endif
        if (!music_player) music_storage_playback_release();
        return music_player != nullptr;
    };
    auto apply_music = [&](MusicCommand command) {
        if (ota_activity_is_active()) return;
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
        if (transport_command != MUSIC_TRANSPORT_STOP && (!active_catalog || !active_catalog->available ||
            (!active_catalog->count && transport_command == MUSIC_TRANSPORT_PLAY_PAUSE))) {
            music_info_set(active_catalog && active_catalog->available
                               ? AUDIO_MUSIC_EMPTY : AUDIO_MUSIC_UNAVAILABLE,
                           active_catalog, music_transport);
            return;
        }
        if (music_transport.has_current_track() && transport_command != MUSIC_TRANSPORT_STOP) {
            uint8_t index = 0;
            while (index < active_catalog->count && strcmp(active_catalog->paths[index], remembered_path)) ++index;
            if (index == active_catalog->count) {
                close_music();
                music_transport.apply(MUSIC_TRANSPORT_FAILURE, active_catalog->count);
                music_info_set(AUDIO_MUSIC_ERROR, active_catalog, music_transport);
                return;
            }
            music_transport.select_track(index);
        }
        const bool interrupted = g_playing.load() || session_guard;
        const MusicTransportResult result = music_transport.apply(transport_command, active_catalog ? active_catalog->count : 0, interrupted);
        if (result.effect != MUSIC_TRANSPORT_NO_EFFECT && music_transport.has_current_track())
            strlcpy(remembered_path, active_catalog->paths[result.track_index], sizeof(remembered_path));
        if (result.effect == MUSIC_TRANSPORT_CLOSE_TRACK) close_music();
        if ((result.effect == MUSIC_TRANSPORT_OPEN_TRACK ||
             (result.effect == MUSIC_TRANSPORT_CLOSE_TRACK &&
              music_transport.state() == MUSIC_TRANSPORT_PLAYING)) &&
            music_transport.has_current_track() && !interrupted) {
            if (!open_music_track(active_catalog->paths[result.track_index])) {
                music_transport.apply(MUSIC_TRANSPORT_FAILURE, active_catalog->count);
            }
        }
        const AudioMusicStatus status = music_transport.state() == MUSIC_TRANSPORT_PLAYING ?
            (interrupted ? AUDIO_MUSIC_INTERRUPTED : AUDIO_MUSIC_PLAYING) :
            music_transport.state() == MUSIC_TRANSPORT_PAUSED ? AUDIO_MUSIC_PAUSED : AUDIO_MUSIC_STOPPED;
        music_info_set(status, active_catalog, music_transport, music_player);
    };
#endif

    for (;;) {
        if (session_guard && !session_guard(session_generation)) session_guard = nullptr;
        if (g_playing.load() && active.guard && !active.guard(active.generation)) close_alert();
        if (g_alert_stop_epoch.load() != alert_stop_epoch) {
            alert_stop_epoch = g_alert_stop_epoch.load();
            close_alert();
        }
        const bool stop_all = g_all_stop_epoch.load() != all_stop_epoch;
        if (stop_all) {
            all_stop_epoch = g_all_stop_epoch.load();
            close_alert();
            tone_alert_overlay_stop(&g_feedback);
        }
#if HAS_SOUND_PLAYER
        if (stop_all || (music_guard && !music_guard(music_generation))) {
            close_music();
            music_transport.apply(MUSIC_TRANSPORT_STOP, 0);
            music_info_set(AUDIO_MUSIC_STOPPED, music_catalog_store_active_for_audio(), music_transport);
            music_guard = nullptr;
        }
#endif
        if (ota_activity_is_active()) {
            close_alert();
            session_guard = nullptr;
            tone_alert_overlay_stop(&g_feedback);
            portENTER_CRITICAL(&g_feedback_mux);
            g_feedback_pending = false;
            portEXIT_CRITICAL(&g_feedback_mux);
#if HAS_SOUND_PLAYER
            close_music();
            music_transport.apply(MUSIC_TRANSPORT_STOP, 0);
            music_info_set(AUDIO_MUSIC_STOPPED, music_catalog_store_active_for_audio(), music_transport);
            MusicWorkCommand discarded_work;
            while (xQueueReceive(music_work_queue, &discarded_work, 0) == pdTRUE) {
                if (discarded_work.kind == MUSIC_WORK_REFRESH) {
                    portENTER_CRITICAL(&g_music_storage_mux);
                    g_music_catalog_refresh_pending = false;
                    portEXIT_CRITICAL(&g_music_storage_mux);
                    xSemaphoreGive(g_music_catalog_refresh_sem);
                }
            }
#endif
            AudioCommand discard;
            while (xQueueReceive(audio_queue, &discard, 0) == pdTRUE) audio_command_dispose(&discard);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        portENTER_CRITICAL(&g_feedback_mux);
        if (g_feedback_pending) {
            g_feedback = g_pending_feedback;
            g_feedback_pending = false;
        }
        portEXIT_CRITICAL(&g_feedback_mux);
    #if HAS_SOUND_PLAYER
        MusicWorkCommand music_work = {};
        if (xQueueReceive(music_work_queue, &music_work, 0) == pdTRUE) {
            const bool valid = !ota_activity_is_active() && music_work.ota_epoch == ota_activity_epoch()
                && music_work.stop_epoch == g_all_stop_epoch.load()
                && (!music_work.guard || music_work.guard(music_work.generation));
            if (music_work.kind == MUSIC_WORK_TRANSPORT) {
                if (valid) {
                    music_guard = music_work.guard;
                    music_generation = music_work.generation;
                    apply_music(music_work.transport);
                }
            } else {
                if (valid) refresh_music_catalog(true);
                portENTER_CRITICAL(&g_music_storage_mux);
                g_music_catalog_refresh_pending = false;
                portEXIT_CRITICAL(&g_music_storage_mux);
                xSemaphoreGive(g_music_catalog_refresh_sem);
            }
        }
#endif
        AudioCommand cmd = {};
        if (xQueueReceive(audio_queue, &cmd, 0) == pdTRUE) {
            if (ota_activity_is_active() || cmd.ota_epoch != ota_activity_epoch()
                || (cmd.kind == AUDIO_COMMAND_PLAY && cmd.stop_epoch != g_alert_stop_epoch.load())
                || (cmd.guard && !cmd.guard(cmd.generation))) { audio_command_dispose(&cmd); continue; }
            if (cmd.kind == AUDIO_COMMAND_STOP_OWNED) {
                if (g_playing.load() && active.guard == cmd.guard && active.generation == cmd.generation) close_alert();
                continue;
            }
            if (cmd.kind == AUDIO_COMMAND_SESSION) {
                session_guard = cmd.guard;
                session_generation = cmd.generation;
#if HAS_SOUND_PLAYER
                close_music();
#endif
                if (g_playing.load() && !active.alarm) close_alert();
                continue;
            }
            if (!cmd.alarm && (session_guard || (g_playing.load() && active.alarm))) {
                audio_command_dispose(&cmd);
                continue;
            }
            ToneAlertOverlay next_tone = {};
#if HAS_SOUND_PLAYER
            const bool tone = !cmd.is_sound && !cmd.is_memory_sound;
#else
            const bool tone = true;
#endif
            if (tone && !tone_alert_overlay_start(&next_tone, cmd.pattern, cmd.loop, AUDIO_SAMPLE_RATE)) {
                audio_command_dispose(&cmd);
                continue;
            }
#if HAS_SOUND_PLAYER
            if (cmd.is_sound && !sound_store_exists(cmd.pattern) && !cmd.alarm) {
                LOGW(TAG, "MP3 alert file unavailable");
                audio_command_dispose(&cmd);
                continue;
            }
#endif
            close_alert();
#if HAS_SOUND_PLAYER
            close_music();
            music_info_set(music_transport.state() == MUSIC_TRANSPORT_PLAYING ? AUDIO_MUSIC_INTERRUPTED :
                music_transport.state() == MUSIC_TRANSPORT_PAUSED ? AUDIO_MUSIC_PAUSED : AUDIO_MUSIC_STOPPED,
                music_catalog_store_active_for_audio(), music_transport);
#endif
            active = cmd;
            primary_tone = next_tone;
            g_playing.store(true);
            g_playback_guard = cmd.guard;
            g_playback_generation = cmd.generation;
#if HAS_SOUND_PLAYER
            if (cmd.is_memory_sound) {
                alert_player = sound_player_begin_memory(output_driver, cmd.mp3, cmd.mp3_size);
            } else if (cmd.is_sound) {
                if (music_storage_playback_claim()) {
                    char path[48];
                    sound_store_path(cmd.pattern, path, sizeof(path));
                    alert_player = sound_player_begin_path(output_driver, path);
                    if (!alert_player) music_storage_playback_release();
                }
            }
            if (!tone && !alert_player) {
                LOGW(TAG, "MP3 alert failed to open");
                if (active.alarm) {
                    tone_alert_overlay_start(&primary_tone, "1000:200 200 1000:200", false, AUDIO_SAMPLE_RATE);
                } else {
                    close_alert();
                }
            }
#endif
        }
        const uint8_t device_volume = current_volume.load();
        const uint8_t requested_volume = g_playing.load() && active.volume_override ? active.volume_override : device_volume;
        if (output_volume != requested_volume) {
            if (!output_driver->setVolume(requested_volume)) LOGW(TAG, "Volume control failed");
            else output_volume = requested_volume;
        }
        if (g_playing.load()) {
#if HAS_SOUND_PLAYER
            if (alert_player) {
                const SoundPlayerStepResult step = sound_player_step(alert_player);
                if (step == SOUND_PLAYER_STEP_COMPLETE && active.loop && sound_player_restart(alert_player)) continue;
                if (step != SOUND_PLAYER_STEP_PLAYING) {
                    if ((step == SOUND_PLAYER_STEP_ERROR || (step == SOUND_PLAYER_STEP_COMPLETE && active.loop)) && active.alarm && (!active.guard || active.guard(active.generation)) && !ota_activity_is_active()) {
                        LOGW(TAG, "MP3 alarm failed; playing fallback tone");
                        sound_player_close(alert_player);
                        alert_player = nullptr;
                        if (active.is_sound) music_storage_playback_release();
                        tone_alert_overlay_start(&primary_tone, "1000:200 200 1000:200", false, AUDIO_SAMPLE_RATE);
                    } else close_alert();
                }
                continue;
            }
#endif
            int16_t frames[512 * 2] = {};
            tone_alert_overlay_mix(&primary_tone, frames, 512, AUDIO_SAMPLE_RATE);
            if (!audio_write_with_stats(output_driver, frames, 512, &tone_stats) || !primary_tone.active) close_alert();
            continue;
        }
#if HAS_SOUND_PLAYER
        const MusicCatalogSnapshot* catalog = music_catalog_store_active_for_audio();
        if (session_guard) {
            music_info_set(music_transport.state() == MUSIC_TRANSPORT_PLAYING ? AUDIO_MUSIC_INTERRUPTED :
                music_transport.state() == MUSIC_TRANSPORT_PAUSED ? AUDIO_MUSIC_PAUSED : AUDIO_MUSIC_STOPPED,
                catalog, music_transport);
        } else if (music_transport.state() == MUSIC_TRANSPORT_PLAYING && !music_player) {
            uint8_t remembered_index = catalog ? catalog->count : 0;
            if (catalog) {
                for (uint8_t index = 0; index < catalog->count; ++index)
                    if (!strcmp(catalog->paths[index], remembered_path)) { remembered_index = index; break; }
            }
            if (!catalog || remembered_index == catalog->count || !open_music_track(remembered_path)) {
                music_transport.apply(MUSIC_TRANSPORT_FAILURE, catalog ? catalog->count : 0);
                music_info_set(AUDIO_MUSIC_ERROR, catalog, music_transport);
            } else {
                music_transport.select_track(remembered_index);
                music_info_set(AUDIO_MUSIC_PLAYING, catalog, music_transport, music_player);
            }
        }
        if (!session_guard && music_player && music_transport.state() == MUSIC_TRANSPORT_PLAYING) {
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
                    if (active_catalog && result.track_index < active_catalog->count)
                        strlcpy(remembered_path, active_catalog->paths[result.track_index], sizeof(remembered_path));
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
        if (music_transport.state() == MUSIC_TRANSPORT_STOPPED) refresh_music_catalog(false);
#endif
        if (g_feedback.active) {
            int16_t silence[512 * 2] = {};
            AudioStarvationStats stats = {};
    #if HAS_SOUND_PLAYER
            if (!music_player || music_transport.state() != MUSIC_TRANSPORT_PLAYING || session_guard)
    #endif
            if (!audio_write_with_stats(output_driver, silence, 512, &stats)) tone_alert_overlay_stop(&g_feedback);
        } else {
    #if HAS_SOUND_PLAYER
            if (!music_player || music_transport.state() != MUSIC_TRANSPORT_PLAYING || session_guard)
    #endif
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
static void audio_init_abort() {
    if (output_driver && !output_driver->end()) LOGW(TAG, "Audio hardware cleanup failed");
    if (audio_queue) {
        vQueueDelete(audio_queue);
        audio_queue = NULL;
    }
#if HAS_SOUND_PLAYER
    if (music_work_queue) {
        vQueueDelete(music_work_queue);
        music_work_queue = NULL;
    }
    if (g_music_catalog_refresh_sem) {
        vSemaphoreDelete(g_music_catalog_refresh_sem);
        g_music_catalog_refresh_sem = nullptr;
    }
#endif
}

void audio_init(uint8_t initial_volume) {
    if (audio_initialized) return;
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
    if (!output_driver->begin(AUDIO_SAMPLE_RATE) ||
        !output_driver->setVolume(current_volume) || !output_driver->isAvailable()) {
        audio_init_abort();
        return;
    }

    // Create command queue and audio task. The stack remains internal because
    // sound playback reads LittleFS while the flash cache is disabled.
    // Priority 5: above LVGL (4) to avoid I2S DMA underruns during heavy rendering
    // Boards can move minimp3's 16 KB scratch workspace to PSRAM and select a
    // smaller internal stack with AUDIO_TASK_STACK_SIZE.
    audio_queue = xQueueCreate(AUDIO_QUEUE_DEPTH, sizeof(AudioCommand));
    if (!audio_queue) {
        LOGE(TAG, "Failed to create audio queue");
        audio_init_abort();
        return;
    }

#if HAS_SOUND_PLAYER
    music_work_queue = xQueueCreate(MUSIC_WORK_QUEUE_DEPTH, sizeof(MusicWorkCommand));
    if (!music_work_queue) {
        LOGE(TAG, "Failed to create Music work queue");
        audio_init_abort();
        return;
    }
    if (!music_catalog_store_init()) {
        LOGE(TAG, "Failed to allocate Music catalog in PSRAM");
    }
    g_music_catalog_refresh_sem = xSemaphoreCreateBinaryStatic(&g_music_catalog_refresh_sem_storage);
    if (!g_music_catalog_refresh_sem) {
        LOGE(TAG, "Failed to create Music catalog refresh semaphore");
        audio_init_abort();
        return;
    }
#endif

    if (!output_driver->setMuted(false)) {
        audio_init_abort();
        return;
    }

    TaskHandle_t created_task = nullptr;
    if (!rtos_create_task_internal_stack_pinned(
            audio_task, "audio", AUDIO_TASK_STACK_SIZE, NULL, 5,
            &created_task, nullptr, 1)) {
        LOGE(TAG, "Failed to create audio task with internal stack");
        audio_init_abort();
        return;
    }

    audio_task_handle.store(created_task, std::memory_order_release);
    audio_initialized = true;
    device_telemetry_log_memory_snapshot("audio");
    LOGI(TAG, "Audio ready (volume=%u%%, PA always-on)", current_volume.load());
}

bool audio_get_stack_watermark(uint32_t* minimum_free_bytes) {
    if (!minimum_free_bytes) return false;
    const TaskHandle_t task = audio_task_handle.load(std::memory_order_acquire);
    if (!task) return false;
    *minimum_free_bytes = uxTaskGetStackHighWaterMark(task);
    return true;
}

void audio_set_volume(uint8_t vol_0_100) {
    if (vol_0_100 > 100) vol_0_100 = 100;
    current_volume = vol_0_100;
    LOGI(TAG, "Volume: %u%%", current_volume.load());
}

uint8_t audio_get_volume() {
    return current_volume;
}

bool audio_submit_tone(const char* pattern, uint8_t volume_override, bool loop,
                       AudioPlaybackGuard guard, uint32_t generation, bool alarm, AudioPlaybackFinished finished) {
    if (ota_activity_is_active()) return false;
    ToneAlertOverlay parsed = {};
    if (!tone_alert_overlay_start(&parsed, pattern, loop, AUDIO_SAMPLE_RATE)) return false;
    if (!audio_initialized) {
        LOGW(TAG, "Audio not initialized");
        return false;
    }

    AudioCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    if (pattern && pattern[0]) {
        strlcpy(cmd.pattern, pattern, AUDIO_PATTERN_MAX_LEN);
    }
    cmd.volume_override = volume_override <= 100 ? volume_override : 0;
    cmd.loop = loop;
    cmd.alarm = alarm || loop;
    cmd.finished = finished;
    cmd.guard = guard;
    cmd.generation = generation;
    cmd.ota_epoch = ota_activity_epoch();
    cmd.stop_epoch = g_alert_stop_epoch.load();

    return xQueueSend(audio_queue, &cmd, 0) == pdTRUE;
}

void audio_beep(const char* pattern, uint8_t volume_override) {
    audio_submit_tone(pattern, volume_override, false);
}

void audio_play_loop(const char* pattern, uint8_t volume_override) {
    audio_submit_tone(pattern, volume_override, true);
}

void audio_feedback(const char* pattern) {
    if (!audio_initialized || ota_activity_is_active() || !pattern || !pattern[0] || !strcmp(pattern, "none")) return;
    ToneAlertOverlay feedback = {};
    if (!tone_alert_overlay_start(&feedback, pattern, false, AUDIO_SAMPLE_RATE, 0.15f)) return;
    uint32_t remaining = AUDIO_SAMPLE_RATE / 4;
    for (uint8_t index = 0; index < feedback.segment_count; ++index) {
        if (feedback.segments[index].frame_count >= remaining) {
            feedback.segments[index].frame_count = remaining;
            feedback.segment_count = index + 1;
            break;
        }
        remaining -= feedback.segments[index].frame_count;
    }
    portENTER_CRITICAL(&g_feedback_mux);
    if (!ota_activity_is_active()) {
        g_pending_feedback = feedback;
        g_feedback_pending = true;
    }
    portEXIT_CRITICAL(&g_feedback_mux);
}

void audio_stop() {
    if (!audio_initialized) return;
    g_alert_stop_epoch.fetch_add(1);
}

void audio_stop_all() {
    audio_stop();
    g_all_stop_epoch.fetch_add(1);
    portENTER_CRITICAL(&g_feedback_mux);
    g_feedback_pending = false;
    portEXIT_CRITICAL(&g_feedback_mux);
}

void audio_stop_owned(AudioPlaybackGuard guard, uint32_t generation) {
    if (!audio_initialized || !guard) return;
    AudioCommand command = {};
    command.kind = AUDIO_COMMAND_STOP_OWNED;
    command.guard = guard;
    command.generation = generation;
    command.ota_epoch = ota_activity_epoch();
    command.stop_epoch = g_alert_stop_epoch.load();
    if (xQueueSend(audio_queue, &command, 0) != pdTRUE) LOGW(TAG, "Owned stop queue full");
}

bool audio_alarm_session(AudioPlaybackGuard guard, uint32_t generation) {
    if (!audio_initialized || ota_activity_is_active() || !guard) return false;
    AudioCommand command = {};
    command.kind = AUDIO_COMMAND_SESSION;
    command.guard = guard;
    command.generation = generation;
    command.ota_epoch = ota_activity_epoch();
    command.stop_epoch = g_alert_stop_epoch.load();
    return xQueueSend(audio_queue, &command, 0) == pdTRUE;
}

bool audio_is_playing() {
    return g_playing;
}

#if HAS_SOUND_PLAYER
AudioMusicSubmitResult audio_music_command(MusicCommand command, AudioPlaybackGuard guard, uint32_t generation) {
    if (command != MUSIC_COMMAND_PLAY_PAUSE && command != MUSIC_COMMAND_NEXT && command != MUSIC_COMMAND_PREVIOUS && command != MUSIC_COMMAND_STOP) return AUDIO_MUSIC_SUBMIT_INVALID;
    if (ota_activity_is_active() && command != MUSIC_COMMAND_STOP) return AUDIO_MUSIC_SUBMIT_BUSY;
    if (!audio_initialized || !music_work_queue) return AUDIO_MUSIC_SUBMIT_UNAVAILABLE;
    portENTER_CRITICAL(&g_music_storage_mux);
    const bool mutating = g_music_storage_mutating;
    portEXIT_CRITICAL(&g_music_storage_mux);
    if (mutating) return AUDIO_MUSIC_SUBMIT_BUSY;
    MusicWorkCommand work = {MUSIC_WORK_TRANSPORT, command};
    work.guard = guard;
    work.generation = generation;
    work.ota_epoch = ota_activity_epoch();
    work.stop_epoch = g_all_stop_epoch.load();
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
    if (ota_activity_is_active()) return false;
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
    command.ota_epoch = ota_activity_epoch();
    command.stop_epoch = g_all_stop_epoch.load();
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
    if (status == AUDIO_MUSIC_PLAYING || status == AUDIO_MUSIC_PAUSED || status == AUDIO_MUSIC_INTERRUPTED) {
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
    audio_submit_sound(filename, volume_override);
}

bool audio_submit_sound(const char* filename, uint8_t volume_override, AudioPlaybackGuard guard, uint32_t generation, bool loop, bool alarm) {
    if (ota_activity_is_active()) return false;
    if (!audio_initialized) {
        LOGW(TAG, "Audio not initialized");
        return false;
    }
    if (!filename || !filename[0]) {
        LOGW(TAG, "Empty sound filename");
        return false;
    }
    portENTER_CRITICAL(&g_music_storage_mux);
    const bool mutating = g_music_storage_mutating;
    portEXIT_CRITICAL(&g_music_storage_mux);
    if (mutating) return false;
    // Note: don't check sound_store_exists() here — it does flash I/O and the
    // caller may be the LVGL task whose stack is in PSRAM (crashes on ESP32-P4).
    // The worker checks file availability before replacing ordinary playback.

    AudioCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    strlcpy(cmd.pattern, filename, AUDIO_PATTERN_MAX_LEN);
    cmd.volume_override = volume_override <= 100 ? volume_override : 0;
    cmd.loop = loop;
    cmd.alarm = alarm || loop;
    cmd.is_sound = true;
    cmd.guard = guard;
    cmd.generation = generation;
    cmd.ota_epoch = ota_activity_epoch();
    cmd.stop_epoch = g_alert_stop_epoch.load();

    return xQueueSend(audio_queue, &cmd, 0) == pdTRUE;
}

void audio_play_mp3_buffer(uint8_t* mp3, size_t mp3_size, uint8_t volume_override,
                           AudioPlaybackGuard guard, uint32_t generation) {
    if (!mp3 || !mp3_size || !audio_initialized || ota_activity_is_active()) {
        if (mp3) heap_caps_free(mp3);
        return;
    }
    AudioCommand cmd = {};
    cmd.volume_override = volume_override <= 100 ? volume_override : 0;
    cmd.is_memory_sound = true;
    cmd.mp3 = mp3;
    cmd.mp3_size = mp3_size;
    cmd.guard = guard;
    cmd.generation = generation;
    cmd.ota_epoch = ota_activity_epoch();
    cmd.stop_epoch = g_alert_stop_epoch.load();
    if (xQueueSend(audio_queue, &cmd, 0) != pdTRUE) audio_command_dispose(&cmd);
}
#endif // HAS_SOUND_PLAYER

#endif // HAS_AUDIO
