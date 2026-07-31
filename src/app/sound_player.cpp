#include "sound_player.h"

#if HAS_SOUND_PLAYER

#ifndef AUDIO_RESAMPLER_TEST
#include "audio.h"
#include "audio_output_driver.h"
#include "storage.h"
#include <esp_heap_caps.h>
#include <string.h>
#include "log_manager.h"
#include "sound_store.h"

// minimp3: single-header MP3 decoder (CC0 license)
#define MINIMP3_NO_SIMD
#define MINIMP3_ONLY_MP3
#define MINIMP3_IMPLEMENTATION
#include "minimp3/minimp3.h"
#endif

#define TAG "SoundPlayer"

// Read buffer for MP3 file data (must hold at least one full MP3 frame)
// MINIMP3 recommends minimum ~16KB for reliable frame detection
#define MP3_READ_BUF_SIZE (16 * 1024)

// The smallest supported MP3 source rate with the largest decoded frame.
#define SOUND_PLAYER_MIN_SRC_RATE 8000
#define SOUND_PLAYER_MAX_SRC_FRAME_SAMPLES 576
const int SOUND_PLAYER_MAX_OUTPUT_FRAMES =
    (SOUND_PLAYER_MAX_SRC_FRAME_SAMPLES * AUDIO_SAMPLE_RATE +
     SOUND_PLAYER_MIN_SRC_RATE - 1) / SOUND_PLAYER_MIN_SRC_RATE;
static_assert(SOUND_PLAYER_MAX_OUTPUT_FRAMES >= 1152,
              "sound-player output buffer must hold a full MPEG-1 frame");

// ---------------------------------------------------------------------------
// Linear interpolation resampler: any source rate → AUDIO_SAMPLE_RATE.
// ---------------------------------------------------------------------------
struct Resampler {
    uint32_t src_rate;
    uint32_t src_channels;
    uint32_t target_rate;
    // Source position numerator over target_rate.
    uint64_t position_numerator;
    // Previous sample pair for interpolation (left, right)
    int16_t prev_l;
    int16_t prev_r;
    int16_t curr_l;
    int16_t curr_r;
};

static void resampler_init(Resampler* r, uint32_t src_rate, uint32_t src_channels,
                           uint32_t target_rate) {
    r->src_rate = src_rate;
    r->src_channels = src_channels;
    r->target_rate = target_rate;
    r->position_numerator = 0;
    r->prev_l = 0;
    r->prev_r = 0;
    r->curr_l = 0;
    r->curr_r = 0;
}

// Resample decoded PCM into stereo 16-bit output at AUDIO_SAMPLE_RATE.
// src: decoded samples (interleaved if stereo)
// src_samples: number of samples per channel
// out: output buffer (stereo interleaved, must hold enough frames)
// max_out_frames: maximum stereo frames to write
// Returns number of stereo frames written.
static int resampler_process(Resampler* r, const int16_t* src, int src_samples,
                             int16_t* out, int max_out_frames, bool* truncated = nullptr) {
    int out_frames = 0;
    const int channels = r->src_channels;

    while (out_frames < max_out_frames) {
        uint32_t int_pos = r->position_numerator / r->target_rate;

        if ((int)int_pos >= src_samples) {
            break;
        }

        uint32_t frac = (uint32_t)((r->position_numerator % r->target_rate) * 65536 / r->target_rate);

        // Get surrounding samples for linear interpolation
        int16_t s0_l, s0_r, s1_l, s1_r;
        if ((int)int_pos == 0 && r->position_numerator < r->src_rate) {
            // At start of buffer — use previous buffer's last sample
            s0_l = r->prev_l;
            s0_r = r->prev_r;
        } else {
            s0_l = src[int_pos * channels];
            s0_r = (channels > 1) ? src[int_pos * channels + 1] : s0_l;
        }

        uint32_t next_pos = int_pos + 1;
        if ((int)next_pos < src_samples) {
            s1_l = src[next_pos * channels];
            s1_r = (channels > 1) ? src[next_pos * channels + 1] : s1_l;
        } else {
            s1_l = s0_l;
            s1_r = s0_r;
        }

        // Linear interpolation
        out[out_frames * 2]     = (int16_t)(s0_l + (int32_t)(s1_l - s0_l) * (int32_t)frac / 65536);
        out[out_frames * 2 + 1] = (int16_t)(s0_r + (int32_t)(s1_r - s0_r) * (int32_t)frac / 65536);
        out_frames++;

        r->position_numerator += r->src_rate;
    }

    const uint64_t frame_numerator = (uint64_t)src_samples * r->target_rate;
    const bool did_truncate = r->position_numerator < frame_numerator;
    if (truncated) *truncated = did_truncate;
    r->position_numerator = r->position_numerator >= frame_numerator
                                ? r->position_numerator - frame_numerator
                                : 0;
    // Save the last samples for interpolation across buffer boundaries.
    if (src_samples > 0) {
        int last = src_samples - 1;
        r->prev_l = src[last * channels];
        r->prev_r = (channels > 1) ? src[last * channels + 1] : src[last * channels];
    }
    return out_frames;
}

// ---------------------------------------------------------------------------
// Play MP3 file — called from audio task
// ---------------------------------------------------------------------------
#ifndef AUDIO_RESAMPLER_TEST
bool sound_player_play(AudioOutputDriver* output_driver, const char* filename,
                       volatile bool* stop_flag) {
    if (!output_driver || !filename || !filename[0]) return false;

    // Build path
    char path[48];
    sound_store_path(filename, path, sizeof(path));

    // Open file
    File file = Storage.open(path, "r");
    if (!file) {
        LOGW(TAG, "File not found: %s", path);
        return false;
    }

    size_t file_size = file.size();
    LOGI(TAG, "Playing %s (%u bytes)", path, (unsigned)file_size);

    // Allocate buffers in PSRAM when available (saves ~35 KB internal RAM).
    // All buffers are CPU-only — no DMA reads them directly.
    auto ps_alloc = [](size_t sz) -> void* {
        void* p = nullptr;
        if (psramFound()) p = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!p)           p = heap_caps_malloc(sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        return p;
    };

    // Read buffer for MP3 file data (16 KB)
    uint8_t* read_buf = (uint8_t*)ps_alloc(MP3_READ_BUF_SIZE);
    if (!read_buf) {
        LOGE(TAG, "Failed to allocate read buffer");
        file.close();
        return false;
    }

    int16_t* out_buf = (int16_t*)ps_alloc(SOUND_PLAYER_MAX_OUTPUT_FRAMES * 2 * sizeof(int16_t));
    if (!out_buf) {
        LOGE(TAG, "Failed to allocate output buffer");
        heap_caps_free(read_buf);
        file.close();
        return false;
    }

    // MP3 decoder state (~6 KB — too large for task stack)
    mp3dec_t* mp3d = (mp3dec_t*)ps_alloc(sizeof(mp3dec_t));
    if (!mp3d) {
        LOGE(TAG, "Failed to allocate MP3 decoder");
        heap_caps_free(out_buf);
        heap_caps_free(read_buf);
        file.close();
        return false;
    }
    mp3dec_init(mp3d);

#if AUDIO_MP3_SCRATCH_PSRAM
    const size_t scratch_size = mp3dec_scratch_size();
    void* decode_scratch = heap_caps_malloc(
        scratch_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!decode_scratch) {
        LOGE(TAG, "Failed to allocate MP3 scratch in PSRAM (%u bytes)",
             (unsigned)scratch_size);
        heap_caps_free(mp3d);
        heap_caps_free(out_buf);
        heap_caps_free(read_buf);
        file.close();
        return false;
    }
#endif

    // PCM decode buffer (4.5 KB)
    int16_t* pcm = (int16_t*)ps_alloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(int16_t));
    if (!pcm) {
        LOGE(TAG, "Failed to allocate PCM buffer");
#if AUDIO_MP3_SCRATCH_PSRAM
        heap_caps_free(decode_scratch);
#endif
        heap_caps_free(mp3d);
        heap_caps_free(out_buf);
        heap_caps_free(read_buf);
        file.close();
        return false;
    }

    // Read initial data
    size_t buf_filled = file.read(read_buf, MP3_READ_BUF_SIZE);
    size_t buf_consumed = 0;
    bool eof = (buf_filled < MP3_READ_BUF_SIZE);

    Resampler resampler;
    bool resampler_initialized = false;
    bool success = true;
    AudioStarvationStats starvation = {};

    while (!(*stop_flag)) {
        // Ensure we have enough data in buffer
        if (buf_consumed > 0) {
            size_t remaining = buf_filled - buf_consumed;
            memmove(read_buf, read_buf + buf_consumed, remaining);
            buf_filled = remaining;
            buf_consumed = 0;

            if (!eof && buf_filled < MP3_READ_BUF_SIZE) {
                size_t to_read = MP3_READ_BUF_SIZE - buf_filled;
                size_t got = file.read(read_buf + buf_filled, to_read);
                buf_filled += got;
                if (got < to_read) eof = true;
            }
        }

        if (buf_filled == 0) break;  // No more data

        // Decode one frame
        mp3dec_frame_info_t frame_info;
    #if AUDIO_MP3_SCRATCH_PSRAM
        int samples = mp3dec_decode_frame_with_scratch(
            mp3d, read_buf + buf_consumed,
            (int)(buf_filled - buf_consumed), pcm, &frame_info,
            decode_scratch);
    #else
        int samples = mp3dec_decode_frame(
            mp3d, read_buf + buf_consumed,
            (int)(buf_filled - buf_consumed), pcm, &frame_info);
    #endif

        if (frame_info.frame_bytes == 0) {
            // No more frames can be decoded
            break;
        }

        buf_consumed += frame_info.frame_bytes;

        if (samples == 0) {
            // Skipped ID3 or invalid data — continue
            continue;
        }

        // Initialize or re-initialize resampler when format changes
        if (!resampler_initialized ||
            (uint32_t)frame_info.hz != resampler.src_rate ||
            (uint32_t)frame_info.channels != resampler.src_channels) {
            resampler_init(&resampler, frame_info.hz, frame_info.channels, AUDIO_SAMPLE_RATE);
            if (!resampler_initialized) {
                LOGD(TAG, "MP3: %d Hz, %d ch, layer %d, %d kbps",
                     frame_info.hz, frame_info.channels, frame_info.layer,
                     frame_info.bitrate_kbps);
            } else {
                LOGD(TAG, "MP3 format change: %d Hz, %d ch",
                     frame_info.hz, frame_info.channels);
            }
            resampler_initialized = true;
        }

        // Resample to AUDIO_SAMPLE_RATE if needed
        int out_frames;
        if ((uint32_t)frame_info.hz == AUDIO_SAMPLE_RATE && frame_info.channels == 2) {
            // No resampling needed, already stereo at target rate
            out_frames = samples;
            memcpy(out_buf, pcm, samples * 2 * sizeof(int16_t));
        } else if ((uint32_t)frame_info.hz == AUDIO_SAMPLE_RATE && frame_info.channels == 1) {
            // Mono → stereo at target rate (no resampling, just dup channels)
            out_frames = samples;
            for (int i = 0; i < samples; i++) {
                out_buf[i * 2]     = pcm[i];
                out_buf[i * 2 + 1] = pcm[i];
            }
        } else {
            // Resample and convert to stereo
            bool truncated = false;
            out_frames = resampler_process(&resampler, pcm, samples,
                                           out_buf, SOUND_PLAYER_MAX_OUTPUT_FRAMES, &truncated);
            if (truncated) {
                LOGW(TAG, "Resampler output cap reached: cap=%d source=%d Hz",
                     SOUND_PLAYER_MAX_OUTPUT_FRAMES, frame_info.hz);
            }
        }

        // Write PCM through the selected output driver.
        if (out_frames > 0) {
            if (!audio_write_with_stats(output_driver, out_buf, out_frames, &starvation)) {
                LOGE(TAG, "Audio output write error");
                success = false;
                break;
            }
        }
    }

    heap_caps_free(pcm);
#if AUDIO_MP3_SCRATCH_PSRAM
    heap_caps_free(decode_scratch);
#endif
    heap_caps_free(mp3d);
    heap_caps_free(out_buf);
    heap_caps_free(read_buf);
    file.close();

    audio_log_starvation(starvation);
    LOGI(TAG, "Playback %s: %s", path, success ? "complete" : "error");
    return success;
}
#endif // AUDIO_RESAMPLER_TEST

#endif // HAS_SOUND_PLAYER
