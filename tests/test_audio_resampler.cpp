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

static void verify_consecutive_frames(uint32_t source_rate, int source_frames,
                                      uint32_t target_rate, int call_count) {
    std::vector<int16_t> source(source_frames * 2);
    for (int sample = 0; sample < source_frames; ++sample) {
        source[sample * 2] = sample;
        source[sample * 2 + 1] = sample;
    }
    const int expected_frames = (int)(((uint64_t)source_frames * target_rate + source_rate - 1) / source_rate);
    check(expected_frames <= SOUND_PLAYER_MAX_OUTPUT_FRAMES, "test case exceeds the production output cap");
    std::vector<int16_t> output(SOUND_PLAYER_MAX_OUTPUT_FRAMES * 2);

    Resampler resampler;
    resampler_init(&resampler, source_rate, 2, target_rate);

    int total = 0;
    int min_call = SOUND_PLAYER_MAX_OUTPUT_FRAMES + 1;
    int16_t previous_last_sample = 0;
    for (int call = 0; call < call_count; ++call) {
        bool truncated = true;
        const int actual_frames = resampler_process(&resampler, source.data(), source_frames,
                                                    output.data(), SOUND_PLAYER_MAX_OUTPUT_FRAMES, &truncated);
        total += actual_frames;
        if (actual_frames < min_call) min_call = actual_frames;
        check(!truncated, "resampler unexpectedly truncated a supported source frame");
        if (call > 0 && source_rate == 8000 && target_rate == AUDIO_SAMPLE_RATE) {
            check(output[0] == previous_last_sample,
                  "resampler did not preserve sample continuity across a frame seam");
        }
        if (actual_frames > 0) previous_last_sample = output[(actual_frames - 1) * 2];
    }

    check(min_call > 0, "resampler returned an empty frame - source frame dropped");

    const uint64_t exact = (uint64_t)source_frames * call_count * target_rate / source_rate;
    check(total >= (int)exact - call_count && total <= (int)exact + call_count,
          "resampler lost or invented frames across consecutive calls");
}

static void verify_undersized_buffer_recovery() {
    const int source_frames = 576;
    std::vector<int16_t> source(source_frames * 2, 1000);
    std::vector<int16_t> output(SOUND_PLAYER_MAX_OUTPUT_FRAMES * 2);

    Resampler resampler;
    resampler_init(&resampler, 8000, 2, AUDIO_SAMPLE_RATE);
    bool truncated = false;
    const int truncated_frames = resampler_process(&resampler, source.data(), source_frames,
                                                   output.data(), 100, &truncated);
    check(truncated_frames == 100, "undersized output buffer did not reach its cap");
    check(truncated, "undersized output buffer did not report truncation");

    const int recovered_frames = resampler_process(&resampler, source.data(), source_frames,
                                                   output.data(), SOUND_PLAYER_MAX_OUTPUT_FRAMES);
    check(recovered_frames > 0, "resampler did not recover after an undersized output buffer");
}

int main() {
    const uint32_t source_rates[] = {8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000};
    const uint32_t target_rates[] = {16000, AUDIO_SAMPLE_RATE};
    int boundary_case_count = 0;
    for (uint32_t target_rate : target_rates) {
        for (uint32_t source_rate : source_rates) {
            const int source_frames = source_rate < 32000 ? 576 : 1152;
            const int required_frames = (int)(((uint64_t)source_frames * target_rate + source_rate - 1) / source_rate);
            if (required_frames == SOUND_PLAYER_MAX_OUTPUT_FRAMES) ++boundary_case_count;
            verify_consecutive_frames(source_rate, source_frames, target_rate, 8);
        }
    }

    check(boundary_case_count > 0, "no source-rate combination exercises the output-cap boundary");
    verify_consecutive_frames(16000, 1152, AUDIO_SAMPLE_RATE, 8);
    verify_undersized_buffer_recovery();

    std::puts("audio resampler consecutive-frame checks passed");
    return 0;
}