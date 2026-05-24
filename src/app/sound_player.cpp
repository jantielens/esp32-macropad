#include "sound_player.h"

#if HAS_SOUND_PLAYER

#include "storage.h"
#include <esp_heap_caps.h>
#include <string.h>
#include "driver/i2s_std.h"
#include "log_manager.h"
#include "sound_store.h"

// minimp3: single-header MP3 decoder (CC0 license)
#define MINIMP3_NO_SIMD
#define MINIMP3_ONLY_MP3
#define MINIMP3_IMPLEMENTATION
#include "minimp3/minimp3.h"

#define TAG "SoundPlayer"

// Target sample rate matches the I2S/ES8311 configuration
static const uint32_t TARGET_RATE = 16000;

// Read buffer for MP3 file data (must hold at least one full MP3 frame)
// MINIMP3 recommends minimum ~16KB for reliable frame detection
#define MP3_READ_BUF_SIZE (16 * 1024)

// ---------------------------------------------------------------------------
// Linear interpolation resampler: any source rate → TARGET_RATE (16 kHz)
// ---------------------------------------------------------------------------
struct Resampler {
    uint32_t src_rate;
    uint32_t src_channels;
    // Fixed-point position in source samples (16.16 format)
    uint32_t pos_frac;
    uint32_t step_frac;  // how much to advance per output sample (16.16)
    // Previous sample pair for interpolation (left, right)
    int16_t prev_l;
    int16_t prev_r;
    int16_t curr_l;
    int16_t curr_r;
};

static void resampler_init(Resampler* r, uint32_t src_rate, uint32_t src_channels) {
    r->src_rate = src_rate;
    r->src_channels = src_channels;
    // step = src_rate / target_rate in 16.16 fixed point
    r->step_frac = (uint32_t)(((uint64_t)src_rate << 16) / TARGET_RATE);
    r->pos_frac = 0;
    r->prev_l = 0;
    r->prev_r = 0;
    r->curr_l = 0;
    r->curr_r = 0;
}

// Resample decoded PCM into stereo 16-bit output at TARGET_RATE.
// src: decoded samples (interleaved if stereo)
// src_samples: number of samples per channel
// out: output buffer (stereo interleaved, must hold enough frames)
// max_out_frames: maximum stereo frames to write
// Returns number of stereo frames written.
static int resampler_process(Resampler* r, const int16_t* src, int src_samples,
                             int16_t* out, int max_out_frames) {
    int out_frames = 0;
    const int channels = r->src_channels;

    while (out_frames < max_out_frames) {
        uint32_t int_pos = r->pos_frac >> 16;
        uint32_t frac = r->pos_frac & 0xFFFF;

        if ((int)int_pos >= src_samples) {
            // Consumed all source samples — update position for next buffer
            r->pos_frac -= (uint32_t)src_samples << 16;
            // Save last samples for interpolation across buffer boundaries
            if (src_samples > 0) {
                int last = src_samples - 1;
                r->prev_l = src[last * channels];
                r->prev_r = (channels > 1) ? src[last * channels + 1] : src[last * channels];
            }
            break;
        }

        // Get surrounding samples for linear interpolation
        int16_t s0_l, s0_r, s1_l, s1_r;
        if ((int)int_pos == 0 && r->pos_frac < r->step_frac) {
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

        r->pos_frac += r->step_frac;
    }

    return out_frames;
}

// ---------------------------------------------------------------------------
// Play MP3 file — called from audio task
// ---------------------------------------------------------------------------
bool sound_player_play(i2s_chan_handle_t tx_handle, const char* filename,
                       volatile bool* stop_flag) {
    if (!tx_handle || !filename || !filename[0]) return false;

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

    // I2S output buffer (stereo, 16-bit, 8 KB)
    // At worst case (48kHz → 16kHz), one MP3 frame (1152 samples) produces
    // 1152 * 16000/48000 = 384 output frames. Add margin.
    const int max_out_frames = 2048;
    int16_t* out_buf = (int16_t*)ps_alloc(max_out_frames * 2 * sizeof(int16_t));
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

    // PCM decode buffer (4.5 KB)
    int16_t* pcm = (int16_t*)ps_alloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(int16_t));
    if (!pcm) {
        LOGE(TAG, "Failed to allocate PCM buffer");
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
        int samples = mp3dec_decode_frame(mp3d, read_buf + buf_consumed,
                                          (int)(buf_filled - buf_consumed),
                                          pcm, &frame_info);

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
            resampler_init(&resampler, frame_info.hz, frame_info.channels);
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

        // Resample to TARGET_RATE if needed
        int out_frames;
        if ((uint32_t)frame_info.hz == TARGET_RATE && frame_info.channels == 2) {
            // No resampling needed, already stereo at target rate
            out_frames = samples;
            memcpy(out_buf, pcm, samples * 2 * sizeof(int16_t));
        } else if ((uint32_t)frame_info.hz == TARGET_RATE && frame_info.channels == 1) {
            // Mono → stereo at target rate (no resampling, just dup channels)
            out_frames = samples;
            for (int i = 0; i < samples; i++) {
                out_buf[i * 2]     = pcm[i];
                out_buf[i * 2 + 1] = pcm[i];
            }
        } else {
            // Resample and convert to stereo
            out_frames = resampler_process(&resampler, pcm, samples,
                                           out_buf, max_out_frames);
        }

        // Write to I2S
        if (out_frames > 0) {
            size_t bytes = out_frames * 2 * sizeof(int16_t);
            size_t written;
            esp_err_t err = i2s_channel_write(tx_handle, out_buf, bytes,
                                              &written, portMAX_DELAY);
            if (err != ESP_OK) {
                LOGE(TAG, "I2S write error: %s", esp_err_to_name(err));
                success = false;
                break;
            }
        }
    }

    heap_caps_free(pcm);
    heap_caps_free(mp3d);
    heap_caps_free(out_buf);
    heap_caps_free(read_buf);
    file.close();

    LOGI(TAG, "Playback %s: %s", path, success ? "complete" : "error");
    return success;
}

#endif // HAS_SOUND_PLAYER
