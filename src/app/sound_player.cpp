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

static bool sound_player_source_rate_supported(uint32_t source_rate) {
    switch (source_rate) {
        case 8000: case 11025: case 12000: case 16000: case 22050:
        case 24000: case 32000: case 44100: case 48000:
            return true;
        default:
            return false;
    }
}

static bool sound_player_duration_us(uint64_t samples, uint32_t source_rate,
                                     uint64_t* duration_us) {
    if (!duration_us || !sound_player_source_rate_supported(source_rate) ||
        samples > UINT64_MAX / 1000000ULL) {
        return false;
    }
    *duration_us = samples * 1000000ULL / source_rate;
    return true;
}

static bool sound_player_duration_add(uint64_t* total_us, uint64_t samples,
                                      uint32_t source_rate) {
    uint64_t duration_us = 0;
    if (!total_us || !sound_player_duration_us(samples, source_rate, &duration_us) ||
        duration_us > UINT64_MAX - *total_us) {
        return false;
    }
    *total_us += duration_us;
    return true;
}

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
struct SoundPlayer {
    AudioOutputDriver* output_driver;
    SoundPlayerPcmTransform transform;
    void* transform_context;
    File file;
    uint8_t* read_buf;
    int16_t* out_buf;
    mp3dec_t* decoder;
#if AUDIO_MP3_SCRATCH_PSRAM
    void* decode_scratch;
#endif
    int16_t* pcm;
    size_t buf_filled;
    size_t buf_consumed;
    bool eof;
    bool resampler_initialized;
    uint64_t total_us;
    uint64_t elapsed_output_frames;
    uint64_t elapsed_us;
    Resampler resampler;
    AudioStarvationStats starvation;
};

static void* sound_player_ps_alloc(size_t size) {
    void* allocation = nullptr;
    if (psramFound()) allocation = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!allocation) allocation = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return allocation;
}

static void sound_player_refill(SoundPlayer* player) {
    if (player->buf_consumed == 0) return;
    const size_t remaining = player->buf_filled - player->buf_consumed;
    memmove(player->read_buf, player->read_buf + player->buf_consumed, remaining);
    player->buf_filled = remaining;
    player->buf_consumed = 0;
    if (!player->eof && player->buf_filled < MP3_READ_BUF_SIZE) {
        const size_t to_read = MP3_READ_BUF_SIZE - player->buf_filled;
        const size_t got = player->file.read(player->read_buf + player->buf_filled, to_read);
        player->buf_filled += got;
        if (got < to_read) player->eof = true;
    }
}

static int sound_player_decode(SoundPlayer* player, int16_t* pcm,
                               mp3dec_frame_info_t* frame_info) {
#if AUDIO_MP3_SCRATCH_PSRAM
    return mp3dec_decode_frame_with_scratch(
        player->decoder, player->read_buf, (int)player->buf_filled, pcm,
        frame_info, player->decode_scratch);
#else
    return mp3dec_decode_frame(
        player->decoder, player->read_buf, (int)player->buf_filled, pcm, frame_info);
#endif
}

static void sound_player_reset_decode(SoundPlayer* player) {
    player->file.seek(0);
    player->buf_filled = player->file.read(player->read_buf, MP3_READ_BUF_SIZE);
    player->buf_consumed = 0;
    player->eof = player->buf_filled < MP3_READ_BUF_SIZE;
    player->resampler_initialized = false;
    mp3dec_init(player->decoder);
}

static void sound_player_record_accepted_output(SoundPlayer* player, int out_frames) {
    if (out_frames <= 0 || (uint64_t)out_frames > UINT64_MAX - player->elapsed_output_frames) {
        return;
    }
    player->elapsed_output_frames += (uint64_t)out_frames;
    if (!sound_player_duration_us(player->elapsed_output_frames, AUDIO_SAMPLE_RATE,
                                  &player->elapsed_us)) {
        player->elapsed_us = 0;
    }
}

void sound_player_close(SoundPlayer* player) {
    if (!player) return;
    if (player->pcm) heap_caps_free(player->pcm);
#if AUDIO_MP3_SCRATCH_PSRAM
    if (player->decode_scratch) heap_caps_free(player->decode_scratch);
#endif
    if (player->decoder) heap_caps_free(player->decoder);
    if (player->out_buf) heap_caps_free(player->out_buf);
    if (player->read_buf) heap_caps_free(player->read_buf);
    if (player->file) player->file.close();
    heap_caps_free(player);
}

SoundPlayer* sound_player_begin_path(AudioOutputDriver* output_driver, const char* path,
                                     SoundPlayerPcmTransform transform,
                                     void* transform_context) {
    if (!output_driver || !path || !path[0]) return nullptr;

    File file = Storage.open(path, "r");
    if (!file) {
        LOGW(TAG, "File not found: %s", path);
        return nullptr;
    }

    const size_t file_size = file.size();
    LOGI(TAG, "Playing %s (%u bytes)", path, (unsigned)file_size);
    SoundPlayer* player = (SoundPlayer*)heap_caps_calloc(1, sizeof(SoundPlayer),
                                                           MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!player) {
        file.close();
        return nullptr;
    }
    player->output_driver = output_driver;
    player->transform = transform;
    player->transform_context = transform_context;
    player->file = file;
    player->read_buf = (uint8_t*)sound_player_ps_alloc(MP3_READ_BUF_SIZE);
    player->out_buf = (int16_t*)sound_player_ps_alloc(SOUND_PLAYER_MAX_OUTPUT_FRAMES * 2 * sizeof(int16_t));
    player->decoder = (mp3dec_t*)sound_player_ps_alloc(sizeof(mp3dec_t));
    player->pcm = (int16_t*)sound_player_ps_alloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(int16_t));
    if (!player->read_buf || !player->out_buf || !player->decoder || !player->pcm) {
        LOGE(TAG, "Failed to allocate MP3 playback buffers");
        sound_player_close(player);
        return nullptr;
    }

#if AUDIO_MP3_SCRATCH_PSRAM
    const size_t scratch_size = mp3dec_scratch_size();
    player->decode_scratch = heap_caps_malloc(
        scratch_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!player->decode_scratch) {
        LOGE(TAG, "Failed to allocate MP3 scratch in PSRAM (%u bytes)",
             (unsigned)scratch_size);
        sound_player_close(player);
        return nullptr;
    }
#endif
    // Start decoding immediately. A full-file duration pass can overflow the
    // internal audio task stack for large Music tracks before playback begins.
    // total_us remains zero to signal an unavailable total duration.
    sound_player_reset_decode(player);
    return player;
}

SoundPlayerStepResult sound_player_step(SoundPlayer* player) {
    if (!player) return SOUND_PLAYER_STEP_ERROR;
    sound_player_refill(player);
    if (player->buf_filled == 0) return SOUND_PLAYER_STEP_COMPLETE;

    mp3dec_frame_info_t frame_info = {};
    const int samples = sound_player_decode(player, player->pcm, &frame_info);
    if (frame_info.frame_bytes == 0) {
        if (player->eof && player->buf_filled == 0) {
            return SOUND_PLAYER_STEP_COMPLETE;
        }
        LOGW(TAG, "MP3 decode stalled before EOF");
        return SOUND_PLAYER_STEP_ERROR;
    }
    player->buf_consumed += frame_info.frame_bytes;
    if (samples == 0) return SOUND_PLAYER_STEP_PLAYING;

    if (!player->resampler_initialized ||
        (uint32_t)frame_info.hz != player->resampler.src_rate ||
        (uint32_t)frame_info.channels != player->resampler.src_channels) {
            resampler_init(&player->resampler, frame_info.hz, frame_info.channels, AUDIO_SAMPLE_RATE);
            if (!player->resampler_initialized) {
                LOGD(TAG, "MP3: %d Hz, %d ch, layer %d, %d kbps",
                     frame_info.hz, frame_info.channels, frame_info.layer,
                     frame_info.bitrate_kbps);
            }
            player->resampler_initialized = true;
    }

    int out_frames;
    if ((uint32_t)frame_info.hz == AUDIO_SAMPLE_RATE && frame_info.channels == 2) {
            // No resampling needed, already stereo at target rate
        out_frames = samples;
        memcpy(player->out_buf, player->pcm, samples * 2 * sizeof(int16_t));
    } else if ((uint32_t)frame_info.hz == AUDIO_SAMPLE_RATE && frame_info.channels == 1) {
            // Mono → stereo at target rate (no resampling, just dup channels)
        out_frames = samples;
        for (int i = 0; i < samples; i++) {
            player->out_buf[i * 2] = player->pcm[i];
            player->out_buf[i * 2 + 1] = player->pcm[i];
        }
    } else {
            // Resample and convert to stereo
            bool truncated = false;
            out_frames = resampler_process(&player->resampler, player->pcm, samples,
                               player->out_buf, SOUND_PLAYER_MAX_OUTPUT_FRAMES, &truncated);
            if (truncated) {
                LOGW(TAG, "Resampler output cap reached: cap=%d source=%d Hz",
                     SOUND_PLAYER_MAX_OUTPUT_FRAMES, frame_info.hz);
            }
    }
    if (out_frames > 0 && player->transform) {
        player->transform(player->transform_context, player->out_buf, out_frames);
    }
    if (out_frames > 0 && !audio_write_with_stats(player->output_driver, player->out_buf,
                                                   out_frames, &player->starvation)) {
        LOGE(TAG, "Audio output write error");
        return SOUND_PLAYER_STEP_ERROR;
    }
    sound_player_record_accepted_output(player, out_frames);
    return SOUND_PLAYER_STEP_PLAYING;
}

bool sound_player_get_timing(const SoundPlayer* player, uint64_t* total_us,
                             uint64_t* elapsed_us) {
    if (!player || !total_us || !elapsed_us) return false;
    *total_us = player->total_us;
    *elapsed_us = player->elapsed_us;
    return true;
}

bool sound_player_play(AudioOutputDriver* output_driver, const char* filename,
                       volatile bool* stop_flag) {
    if (!filename || !filename[0]) return false;
    char path[48];
    sound_store_path(filename, path, sizeof(path));
    SoundPlayer* player = sound_player_begin_path(output_driver, path);
    if (!player) return false;
    SoundPlayerStepResult result = SOUND_PLAYER_STEP_PLAYING;
    while (!*stop_flag && result == SOUND_PLAYER_STEP_PLAYING) {
        result = sound_player_step(player);
    }
    audio_log_starvation(player->starvation);
    sound_player_close(player);
    return result != SOUND_PLAYER_STEP_ERROR;
}
#endif // AUDIO_RESAMPLER_TEST

#endif // HAS_SOUND_PLAYER
