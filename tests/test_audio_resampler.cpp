#include <cstdio>
#include <cstdlib>
#include <vector>

#define HAS_SOUND_PLAYER 1
#define AUDIO_RESAMPLER_TEST 1
#include "../src/app/sound_player.cpp"

static void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static void verify_frame_count(uint32_t source_rate, int source_frames, uint32_t target_rate) {
    constexpr int max_output_frames = 3456;
    std::vector<int16_t> source(source_frames * 2, 1000);
    const int expected_frames = (int)(((uint64_t)source_frames * target_rate + source_rate - 1) / source_rate);
    check(expected_frames <= max_output_frames, "test case exceeds the production output cap");
    std::vector<int16_t> output(max_output_frames * 2);

    Resampler resampler;
    resampler_init(&resampler, source_rate, 2, target_rate);
    const int actual_frames = resampler_process(&resampler, source.data(), source_frames,
                                                output.data(), max_output_frames);
    check(actual_frames == expected_frames, "resampler truncated or emitted an incorrect frame count");
}

int main() {
    const uint32_t source_rates[] = {8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000};
    const uint32_t target_rates[] = {16000, 48000};
    for (uint32_t target_rate : target_rates) {
        for (uint32_t source_rate : source_rates) {
            const int source_frames = (source_rate == 8000 || source_rate == 11025 || source_rate == 12000) ? 576 : 1152;
            verify_frame_count(source_rate, source_frames, target_rate);
        }
    }
    std::puts("audio resampler frame-count checks passed");
    return 0;
}