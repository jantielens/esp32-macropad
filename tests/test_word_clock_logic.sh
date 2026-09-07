#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

compiler=${CXX:-g++}
binary=$(mktemp)
trap 'rm -f "$binary"' EXIT

"$compiler" -std=c++17 -Wall -Wextra -Werror -Iextensions/word-clock -x c++ - -o "$binary" <<'CPP'
#include "word_clock_phrase.h"

#include <cstdio>

bool contains(const WordClockWord* words, uint8_t count, WordClockWord word) {
    for (uint8_t index = 0; index < count; ++index) if (words[index] == word) return true;
    return false;
}

bool contains_accurate(const WordClockAccurateWord* words, uint8_t count, WordClockAccurateWord word) {
    for (uint8_t index = 0; index < count; ++index) if (words[index] == word) return true;
    return false;
}

bool expected_active(WordClockWord word, uint8_t hour, uint8_t minute, WordClockMode mode) {
    static constexpr WordClockWord minute_words[12][2] = {
        {}, {WORD_CLOCK_FIVE_MINUTES}, {WORD_CLOCK_TEN_MINUTES}, {WORD_CLOCK_QUARTER},
        {WORD_CLOCK_TWENTY}, {WORD_CLOCK_TWENTY, WORD_CLOCK_FIVE_MINUTES}, {WORD_CLOCK_HALF},
        {WORD_CLOCK_TWENTY, WORD_CLOCK_FIVE_MINUTES}, {WORD_CLOCK_TWENTY}, {WORD_CLOCK_QUARTER},
        {WORD_CLOCK_TEN_MINUTES}, {WORD_CLOCK_FIVE_MINUTES},
    };
    static constexpr uint8_t minute_counts[12] = {0, 1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1};
    const uint8_t slot = mode == WORD_CLOCK_MODE_MINUTE_DOTS ? minute / 5u : (minute + 2u) / 5u;
    const uint8_t phrase = slot % 12u;
    const bool next_hour = slot >= 7u;
    const uint8_t hour_value = static_cast<uint8_t>((hour + (next_hour ? 1u : 0u)) % 12u);
    static constexpr WordClockWord hour_words[12] = {
        WORD_CLOCK_TWELVE, WORD_CLOCK_ONE, WORD_CLOCK_TWO, WORD_CLOCK_THREE,
        WORD_CLOCK_FOUR, WORD_CLOCK_FIVE_HOUR, WORD_CLOCK_SIX, WORD_CLOCK_SEVEN,
        WORD_CLOCK_EIGHT, WORD_CLOCK_NINE, WORD_CLOCK_TEN_HOUR, WORD_CLOCK_ELEVEN,
    };
    if (word == WORD_CLOCK_IT || word == WORD_CLOCK_IS) return true;
    if (word == hour_words[hour_value]) return true;
    if (phrase == 0) return word == WORD_CLOCK_OCLOCK;
    if (word == (next_hour ? WORD_CLOCK_TO : WORD_CLOCK_PAST)) return true;
    return contains(minute_words[phrase], minute_counts[phrase], word);
}

bool expected_accurate_active(WordClockAccurateWord word, uint8_t hour, uint8_t minute) {
    static constexpr WordClockAccurateWord minute_words[31][2] = {
        {}, {WORD_CLOCK_ACCURATE_MINUTE_ONE}, {WORD_CLOCK_ACCURATE_MINUTE_TWO},
        {WORD_CLOCK_ACCURATE_MINUTE_THREE}, {WORD_CLOCK_ACCURATE_MINUTE_FOUR},
        {WORD_CLOCK_ACCURATE_MINUTE_FIVE}, {WORD_CLOCK_ACCURATE_MINUTE_SIX},
        {WORD_CLOCK_ACCURATE_MINUTE_SEVEN}, {WORD_CLOCK_ACCURATE_MINUTE_EIGHT},
        {WORD_CLOCK_ACCURATE_MINUTE_NINE}, {WORD_CLOCK_ACCURATE_MINUTE_TEN},
        {WORD_CLOCK_ACCURATE_MINUTE_ELEVEN}, {WORD_CLOCK_ACCURATE_MINUTE_TWELVE},
        {WORD_CLOCK_ACCURATE_THIRTEEN}, {WORD_CLOCK_ACCURATE_FOURTEEN},
        {WORD_CLOCK_ACCURATE_QUARTER}, {WORD_CLOCK_ACCURATE_SIXTEEN},
        {WORD_CLOCK_ACCURATE_SEVENTEEN}, {WORD_CLOCK_ACCURATE_EIGHTEEN},
        {WORD_CLOCK_ACCURATE_NINETEEN}, {WORD_CLOCK_ACCURATE_TWENTY},
        {WORD_CLOCK_ACCURATE_TWENTY, WORD_CLOCK_ACCURATE_MINUTE_ONE},
        {WORD_CLOCK_ACCURATE_TWENTY, WORD_CLOCK_ACCURATE_MINUTE_TWO},
        {WORD_CLOCK_ACCURATE_TWENTY, WORD_CLOCK_ACCURATE_MINUTE_THREE},
        {WORD_CLOCK_ACCURATE_TWENTY, WORD_CLOCK_ACCURATE_MINUTE_FOUR},
        {WORD_CLOCK_ACCURATE_TWENTY, WORD_CLOCK_ACCURATE_MINUTE_FIVE},
        {WORD_CLOCK_ACCURATE_TWENTY, WORD_CLOCK_ACCURATE_MINUTE_SIX},
        {WORD_CLOCK_ACCURATE_TWENTY, WORD_CLOCK_ACCURATE_MINUTE_SEVEN},
        {WORD_CLOCK_ACCURATE_TWENTY, WORD_CLOCK_ACCURATE_MINUTE_EIGHT},
        {WORD_CLOCK_ACCURATE_TWENTY, WORD_CLOCK_ACCURATE_MINUTE_NINE},
        {WORD_CLOCK_ACCURATE_HALF},
    };
    static constexpr uint8_t minute_counts[31] = {
        0,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        2, 2, 2, 2, 2, 2, 2, 2, 2,
        1,
    };
    const bool next_hour = minute > 30u;
    const uint8_t displayed_minute = next_hour ? static_cast<uint8_t>(60u - minute) : minute;
    const uint8_t hour_value = static_cast<uint8_t>((hour + (next_hour ? 1u : 0u)) % 12u);
    const WordClockAccurateWord hour_word = static_cast<WordClockAccurateWord>(
        WORD_CLOCK_ACCURATE_HOUR_ONE + (hour_value ? hour_value - 1u : 11u));
    if (word == WORD_CLOCK_ACCURATE_IT || word == WORD_CLOCK_ACCURATE_IS) return true;
    if (word == hour_word) return true;
    if (displayed_minute == 0u) return word == WORD_CLOCK_ACCURATE_OCLOCK;
    if (word == (next_hour ? WORD_CLOCK_ACCURATE_TO : WORD_CLOCK_ACCURATE_PAST)) return true;
    return contains_accurate(minute_words[displayed_minute], minute_counts[displayed_minute], word);
}

int main() {
    int failures = 0;
    for (uint8_t mode = WORD_CLOCK_MODE_ROUNDED; mode <= WORD_CLOCK_MODE_MINUTE_DOTS; ++mode) {
        for (uint8_t hour = 0; hour < 12; ++hour) {
            for (uint8_t minute = 0; minute < 60; ++minute) {
                const WordClockMode selected_mode = static_cast<WordClockMode>(mode);
                const uint8_t expected_dots = selected_mode == WORD_CLOCK_MODE_MINUTE_DOTS ? minute % 5u : 0;
                const uint8_t actual_dots = word_clock_minute_dots(minute, selected_mode);
                if (expected_dots != actual_dots) {
                    std::fprintf(stderr, "FAIL: mode %u %02u:%02u expected %u dots, got %u\n",
                                 mode, hour, minute, expected_dots, actual_dots);
                    ++failures;
                }
                for (uint8_t value = WORD_CLOCK_IT; value <= WORD_CLOCK_OCLOCK; ++value) {
                    const WordClockWord word = static_cast<WordClockWord>(value);
                    const bool expected = expected_active(word, hour, minute, selected_mode);
                    const bool actual = word_clock_word_is_active(word, hour, minute, selected_mode);
                    if (expected == actual) continue;
                    std::fprintf(stderr, "FAIL: mode %u %02u:%02u word %u expected %u, got %u\n",
                                 mode, hour, minute, value, expected, actual);
                    ++failures;
                }
            }
        }
    }
    for (uint8_t hour = 0; hour < 12; ++hour) {
        for (uint8_t minute = 0; minute < 60; ++minute) {
            for (uint8_t value = WORD_CLOCK_ACCURATE_IT; value <= WORD_CLOCK_ACCURATE_OCLOCK; ++value) {
                const WordClockAccurateWord word = static_cast<WordClockAccurateWord>(value);
                const bool expected = expected_accurate_active(word, hour, minute);
                const bool actual = word_clock_accurate_word_is_active(word, hour, minute);
                if (expected == actual) continue;
                std::fprintf(stderr, "FAIL: accurate %02u:%02u word %u expected %u, got %u\n",
                             hour, minute, value, expected, actual);
                ++failures;
            }
        }
    }
    if (failures) return 1;
    std::puts("PASS: word clock phrase logic covers all three modes and all 2160 minute states.");
    return 0;
}
CPP

"$binary"