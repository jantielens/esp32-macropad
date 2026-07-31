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

struct AudioCommand {
    char pattern[AUDIO_PATTERN_MAX_LEN];
    uint8_t volume_override; // 0 = use current
    bool loop;               // true = repeat until stop
#if HAS_SOUND_PLAYER
    bool is_sound;           // true = play sound file (pattern holds filename)
#endif
};

static QueueHandle_t audio_queue = NULL;
static TaskHandle_t audio_task_handle = NULL;
// Cross-task flags: written by audio_enqueue()/audio_stop() (caller task),
// read/written by audio_task (FreeRTOS task).  volatile provides visibility;
// no mutex needed because the only race (g_playing read in audio_enqueue vs
// g_playing write in audio_task) is benign — a spurious g_stop_requested=true
// when nothing is playing is harmlessly cleared on the next queue receive.
static volatile bool g_stop_requested = false;
static volatile bool g_playing = false;

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

    for (;;) {
        if (xQueueReceive(audio_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            g_stop_requested = false;
            g_playing = true;

            uint8_t play_vol = current_volume;
            if (cmd.volume_override > 0 && cmd.volume_override <= 100) {
                play_vol = cmd.volume_override;
            }
            LOGD(TAG, "Play: vol=%u%% (override=%u, device=%u) loop=%d", play_vol, cmd.volume_override, current_volume, cmd.loop);
            output_driver->setVolume(play_vol);

#if HAS_SOUND_PLAYER
            if (cmd.is_sound) {
                sound_player_play(output_driver, cmd.pattern, &g_stop_requested);
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
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void audio_init(uint8_t initial_volume) {
    LOGI(TAG, "Initializing audio output");

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
    while (xQueueReceive(audio_queue, &discard, 0) == pdTRUE) {}

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
    while (xQueueReceive(audio_queue, &discard, 0) == pdTRUE) {}
    LOGD(TAG, "Stop requested");
}

bool audio_is_playing() {
    return g_playing;
}

#if HAS_SOUND_PLAYER
void audio_play_sound(const char* filename, uint8_t volume_override) {
    if (!audio_initialized) {
        LOGW(TAG, "Audio not initialized");
        return;
    }
    if (!filename || !filename[0]) {
        LOGW(TAG, "Empty sound filename");
        return;
    }
    // Note: don't check sound_store_exists() here — it does flash I/O and the
    // caller may be the LVGL task whose stack is in PSRAM (crashes on ESP32-P4).
    // sound_player_play() handles file-not-found gracefully.

    // Stop any current playback
    if (g_playing) {
        g_stop_requested = true;
    }
    AudioCommand discard;
    while (xQueueReceive(audio_queue, &discard, 0) == pdTRUE) {}

    AudioCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    strlcpy(cmd.pattern, filename, AUDIO_PATTERN_MAX_LEN);
    cmd.volume_override = volume_override;
    cmd.loop = false;
    cmd.is_sound = true;

    xQueueSend(audio_queue, &cmd, portMAX_DELAY);
}
#endif // HAS_SOUND_PLAYER

#endif // HAS_AUDIO
