#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

compiler=${CXX:-g++}
binary=$(mktemp)
trap 'rm -f "$binary"' EXIT

"$compiler" -std=c++17 -Wall -Wextra -Werror -Iextensions/word-clock -x c++ - -o "$binary" <<'CPP'
#include "word_clock_face.h"

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

bool expected_accurate_active(WordClockAccurateWord word, uint8_t hour, uint8_t minute,
                              uint8_t past_threshold_minutes) {
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
    const bool next_hour = minute > past_threshold_minutes;
    const uint8_t displayed_minute = next_hour ? static_cast<uint8_t>(60u - minute) : minute;
    const uint8_t hour_value = static_cast<uint8_t>((hour + (next_hour ? 1u : 0u)) % 12u);
    const WordClockAccurateWord hour_word = static_cast<WordClockAccurateWord>(
        WORD_CLOCK_ACCURATE_HOUR_ONE + (hour_value ? hour_value - 1u : 11u));
    if (word == WORD_CLOCK_ACCURATE_IT || word == WORD_CLOCK_ACCURATE_IS) return true;
    if (word == hour_word) return true;
    if (displayed_minute == 0u) return word == WORD_CLOCK_ACCURATE_OCLOCK;
    if (word == (next_hour ? WORD_CLOCK_ACCURATE_TO : WORD_CLOCK_ACCURATE_PAST)) return true;
    if (displayed_minute == 15u || displayed_minute == 30u)
        return contains_accurate(minute_words[displayed_minute], minute_counts[displayed_minute], word);
    if (word == WORD_CLOCK_ACCURATE_MINUTE) return true;
    if (word == WORD_CLOCK_ACCURATE_MINUTE_PLURAL) return displayed_minute != 1u;
    if (displayed_minute <= 29u)
        return contains_accurate(minute_words[displayed_minute], minute_counts[displayed_minute], word);
    const WordClockAccurateWord tens = static_cast<WordClockAccurateWord>(
        WORD_CLOCK_ACCURATE_TWENTY + displayed_minute / 10u - 2u);
    if (word == tens) return true;
    const uint8_t units = displayed_minute % 10u;
    return units && word == static_cast<WordClockAccurateWord>(WORD_CLOCK_ACCURATE_MINUTE_ONE + units - 1u);
}

uint8_t accurate_phrase(WordClockAccurateWord* words, uint8_t hour, uint8_t minute,
                        uint8_t past_threshold_minutes) {
    const bool next_hour = minute > past_threshold_minutes;
    const uint8_t displayed_minute = next_hour ? static_cast<uint8_t>(60u - minute) : minute;
    const WordClockAccurateWord displayed_hour = word_clock_accurate_hour_word(
        static_cast<uint8_t>(hour + (next_hour ? 1u : 0u)));
    uint8_t count = 0;
    words[count++] = WORD_CLOCK_ACCURATE_IT;
    words[count++] = WORD_CLOCK_ACCURATE_IS;
    if (displayed_minute == 0u) {
        words[count++] = displayed_hour;
        words[count++] = WORD_CLOCK_ACCURATE_OCLOCK;
        return count;
    }
    if (displayed_minute == 15u) words[count++] = WORD_CLOCK_ACCURATE_QUARTER;
    else if (displayed_minute == 30u) words[count++] = WORD_CLOCK_ACCURATE_HALF;
    else {
        if (displayed_minute >= 20u) words[count++] = word_clock_accurate_tens_word(displayed_minute);
        const uint8_t units = static_cast<uint8_t>(displayed_minute % 10u);
        if (displayed_minute < 20u || units) words[count++] = word_clock_accurate_number_word(
            displayed_minute < 20u ? displayed_minute : units);
        words[count++] = WORD_CLOCK_ACCURATE_MINUTE;
        if (displayed_minute != 1u) words[count++] = WORD_CLOCK_ACCURATE_MINUTE_PLURAL;
    }
    words[count++] = next_hour ? WORD_CLOCK_ACCURATE_TO : WORD_CLOCK_ACCURATE_PAST;
    words[count++] = displayed_hour;
    return count;
}

int main() {
    int failures = 0;
    static constexpr uint8_t font_sizes[] = {12, 14, 18, 24, 32, 36, 48};
    static constexpr uint8_t expected_shifts[] = {4, 4, 6, 8, 10, 12, 16};
    for (uint8_t index = 0; index < sizeof(font_sizes); ++index) {
        const uint8_t actual = word_clock_burn_in_shift_pixels(font_sizes[index], 0);
        if (actual != expected_shifts[index]) {
            std::fprintf(stderr, "FAIL: font size %u expected shift %u, got %u\n",
                         font_sizes[index], expected_shifts[index], actual);
            ++failures;
        }
    }
    if (word_clock_burn_in_shift_pixels(12, 7) != 7) {
        std::fprintf(stderr, "FAIL: configured burn-in shift was not preserved\n");
        ++failures;
    }
    for (uint8_t phase = 0; phase < 9; ++phase) {
        const int16_t x = word_clock_burn_in_shift_x(phase, 4);
        const int16_t y = word_clock_burn_in_shift_y(phase, 4);
        if (x < -4 || x > 4 || y < -4 || y > 4 || x % 4 || y % 4) {
            std::fprintf(stderr, "FAIL: shift phase %u produced (%d, %d)\n", phase, x, y);
            ++failures;
        }
    }
    uint8_t shift_phase = 0;
    for (uint8_t expected = 1; expected <= 9; ++expected) {
        shift_phase = word_clock_burn_in_next_phase(shift_phase);
        const uint8_t actual = static_cast<uint8_t>(expected % 9u);
        if (shift_phase != actual) {
            std::fprintf(stderr, "FAIL: burn-in phase %u expected %u, got %u\n", expected, actual, shift_phase);
            ++failures;
        }
    }
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
    for (uint8_t threshold = 30; threshold <= 44; ++threshold) {
        for (uint8_t hour = 0; hour < 12; ++hour) {
            for (uint8_t minute = 0; minute < 60; ++minute) {
                for (uint8_t value = WORD_CLOCK_ACCURATE_IT; value <= WORD_CLOCK_ACCURATE_OCLOCK; ++value) {
                    const WordClockAccurateWord word = static_cast<WordClockAccurateWord>(value);
                    const bool expected = expected_accurate_active(word, hour, minute, threshold);
                    const bool actual = word_clock_accurate_word_is_active(word, hour, minute, threshold);
                    if (expected == actual) continue;
                    std::fprintf(stderr, "FAIL: accurate threshold %u %02u:%02u word %u expected %u, got %u\n",
                                 threshold, hour, minute, value, expected, actual);
                    ++failures;
                }
                WordClockAccurateWord phrase[8] = {};
                const uint8_t word_count = accurate_phrase(phrase, hour, minute, threshold);
                for (uint8_t index = 1; index < word_count; ++index) {
                    if (word_clock_accurate_word_position(phrase[index - 1]) <
                        word_clock_accurate_word_position(phrase[index])) continue;
                    std::fprintf(stderr, "FAIL: accurate threshold %u %02u:%02u visual order word %u before %u\n",
                                 threshold, hour, minute, phrase[index - 1], phrase[index]);
                    ++failures;
                }
            }
        }
    }
    if (failures) return 1;
    std::puts("PASS: word clock phrase logic covers all modes and accurate thresholds.");
    return 0;
}
CPP

"$binary"