#include <cstdio>
#include <cstdlib>

#include "drivers/audio_gain.h"

static void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

int main() {
    static_assert(sizeof(AUDIO_GAIN_Q15) / sizeof(AUDIO_GAIN_Q15[0]) == 101,
                  "gain table must contain 101 entries");
    check(audio_gain_apply(12345, 0) == 0, "volume zero must be silent");
    check(audio_gain_apply(12345, 100) == 12345, "volume 100 must pass through unchanged");
    for (size_t index = 1; index < 101; ++index) {
        check(AUDIO_GAIN_Q15[index] >= AUDIO_GAIN_Q15[index - 1], "gain table is not monotonic");
        check(AUDIO_GAIN_Q15[index] <= 32768, "gain table exceeds unity");
    }
    std::puts("audio gain checks passed");
    return 0;
}