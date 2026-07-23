#include <cstdio>
#include <cstdlib>
#include <vector>

#define MINIMP3_NO_SIMD
#define MINIMP3_ONLY_MP3
#define MINIMP3_IMPLEMENTATION
#include "minimp3/minimp3.h"

static void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

int main() {
    const size_t scratch_size = mp3dec_scratch_size();
    check(scratch_size == sizeof(mp3dec_scratch_t),
          "reported scratch size differs from decoder workspace");
    check(scratch_size >= 16 * 1024 - 256 && scratch_size <= 16 * 1024 + 256,
          "decoder scratch size moved outside the expected 16 KB range");

    const uint8_t invalid_mp3[16] = {0};
    mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME] = {0};

    mp3dec_t external_decoder;
    mp3dec_init(&external_decoder);
    mp3dec_frame_info_t external_info = {};
    std::vector<uint8_t> scratch(scratch_size);
    const int external_samples = mp3dec_decode_frame_with_scratch(
        &external_decoder, invalid_mp3, sizeof(invalid_mp3), pcm,
        &external_info, scratch.data());

    mp3dec_t legacy_decoder;
    mp3dec_init(&legacy_decoder);
    mp3dec_frame_info_t legacy_info = {};
    const int legacy_samples = mp3dec_decode_frame(
        &legacy_decoder, invalid_mp3, sizeof(invalid_mp3), pcm, &legacy_info);

    check(external_samples == legacy_samples,
          "external and legacy decode results differ");
    check(external_info.frame_bytes == legacy_info.frame_bytes,
          "external and legacy frame consumption differs");

    external_info.frame_bytes = 123;
    const int null_samples = mp3dec_decode_frame_with_scratch(
        &external_decoder, invalid_mp3, sizeof(invalid_mp3), pcm,
        &external_info, nullptr);
    check(null_samples == 0, "null scratch must reject decoding");
    check(external_info.frame_bytes == 0,
          "null scratch must clear consumed frame count");

    std::printf("minimp3 external scratch: %zu bytes, checks passed\n",
                scratch_size);
    return 0;
}