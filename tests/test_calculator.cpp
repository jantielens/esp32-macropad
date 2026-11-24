#include <gtest/gtest.h>
#include <cmath>
#include <cstdio>

#include "../extensions/calculator/calculator_logic.h"

using namespace calculator;

static void keys(State& state, std::initializer_list<Key> sequence) {
    for (Key key : sequence) press(&state, key, std::sqrt);
}

TEST(Calculator, ImmediateArithmeticAndPercent) {
    State state{};
    reset(&state);
    keys(state, {Digit2, Add, Digit3, Multiply, Digit4, Equals});
    EXPECT_FLOAT_EQ(state.value, 20.0f);
    reset(&state);
    keys(state, {Digit2, Digit0, Digit0, Add, Digit1, Digit0, Percent, Equals});
    EXPECT_FLOAT_EQ(state.value, 220.0f);
    reset(&state);
    keys(state, {Digit2, Digit0, Digit0, Multiply, Digit1, Digit0, Percent, Equals});
    EXPECT_FLOAT_EQ(state.value, 20.0f);
}

TEST(Calculator, TransformedOperandAndNewCalculation) {
    State state{};
    reset(&state);
    keys(state, {Digit2, Add, Digit9, SquareRoot, Multiply, Digit4, Equals});
    EXPECT_FLOAT_EQ(state.value, 20.0f);
    keys(state, {Digit7, Equals});
    EXPECT_FLOAT_EQ(state.value, 7.0f);
}

TEST(Calculator, MemoryAndErrors) {
    State state{};
    reset(&state);
    keys(state, {Digit5, MemoryAdd, Clear, MemoryRecall, Add, Digit2, Equals});
    EXPECT_FLOAT_EQ(state.value, 7.0f);
    keys(state, {Divide, Digit0, Equals});
    EXPECT_TRUE(state.error);
    keys(state, {Clear, Digit9, Sign, SquareRoot});
    EXPECT_TRUE(state.error);
    keys(state, {Clear, MemoryRecall});
    EXPECT_FLOAT_EQ(state.value, 5.0f);
    keys(state, {Digit9, Add, Digit1, Equals, MemoryRecall, Equals});
    EXPECT_FLOAT_EQ(state.value, 5.0f);
}

TEST(Calculator, FormatsRepeatedIntegerResultsWithoutRoundingArtifacts) {
    State state{};
    reset(&state);
    keys(state, {Digit0, Add, Digit3});
    for (int i = 1; i <= 50; ++i) {
        keys(state, {Equals});
        char actual[32];
        char expected[32];
        display(state, actual);
        std::snprintf(expected, sizeof(expected), "%d", i * 3);
        EXPECT_STREQ(actual, expected) << "iteration " << i;
    }
}

TEST(Calculator, FormatsFractionsAndScientificNotation) {
    State state{};
    char actual[32];
    state.value = -0.125f;
    display(state, actual);
    EXPECT_STREQ(actual, "-0.125");
    state.value = 12345678.0f;
    display(state, actual);
    EXPECT_STREQ(actual, "1.234568e+7");
    state.value = 0.0000125f;
    display(state, actual);
    EXPECT_STREQ(actual, "1.25e-5");
    state.value = 165093.046875f;
    display(state, actual);
    EXPECT_STREQ(actual, "165093");
    state.value = 2.75477243e15f;
    display(state, actual);
    EXPECT_STREQ(actual, "2.754772e+15");
}

TEST(Calculator, LayoutRequiresUsableKeys) {
    Layout result{};
    EXPECT_FALSE(layout(229, 260, &result));
    EXPECT_TRUE(layout(230, 260, &result));
    EXPECT_GE(result.key_width, kMinKeyWidth);
    EXPECT_GE(result.key_height, kMinKeyHeight);
}
