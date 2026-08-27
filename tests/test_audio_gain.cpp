#include <gtest/gtest.h>

#include "drivers/audio_gain.h"

TEST(AudioGain, AppliesVolumeAndPreservesGainTableInvariants) {
    static_assert(sizeof(AUDIO_GAIN_Q15) / sizeof(AUDIO_GAIN_Q15[0]) == 101,
                  "gain table must contain 101 entries");
    EXPECT_EQ(audio_gain_apply(12345, 0), 0);
    EXPECT_EQ(audio_gain_apply(12345, 100), 12345);
    for (size_t index = 1; index < 101; ++index) {
        EXPECT_GE(AUDIO_GAIN_Q15[index], AUDIO_GAIN_Q15[index - 1]);
        EXPECT_LE(AUDIO_GAIN_Q15[index], 32768);
    }
}