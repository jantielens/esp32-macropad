#pragma once

#include "word_clock_phrase.h"

struct WordClockFaceRange { uint8_t row, column, length; };

constexpr uint8_t WORD_CLOCK_ACCURATE_GRID_COLUMNS = 18;

constexpr WordClockFaceRange WORD_CLOCK_ACCURATE_WORD_RANGES[] = {
    {0, 0, 2}, {0, 3, 2}, {10, 0, 6}, {10, 6, 1}, {1, 12, 3}, {2, 0, 3},
    {2, 4, 5}, {2, 10, 4}, {3, 0, 4}, {3, 5, 3}, {3, 9, 5}, {4, 0, 5},
    {4, 6, 4}, {4, 11, 3}, {5, 0, 6}, {5, 7, 6}, {6, 0, 8}, {7, 0, 8},
    {6, 9, 7}, {7, 9, 7}, {8, 0, 9}, {9, 0, 8}, {9, 9, 8}, {0, 6, 6},
    {0, 12, 6}, {1, 0, 5}, {1, 6, 5}, {10, 8, 7}, {11, 0, 4}, {11, 5, 4},
    {11, 10, 2}, {12, 0, 3}, {12, 4, 3}, {12, 8, 5}, {13, 0, 4}, {13, 5, 4},
    {13, 10, 3}, {14, 0, 5}, {14, 6, 5}, {15, 0, 4}, {15, 5, 3}, {15, 9, 6},
    {16, 0, 6}, {16, 7, 6},
};

static inline uint16_t word_clock_accurate_word_position(WordClockAccurateWord word) {
    const WordClockFaceRange& range = WORD_CLOCK_ACCURATE_WORD_RANGES[word];
    return static_cast<uint16_t>(range.row) * WORD_CLOCK_ACCURATE_GRID_COLUMNS + range.column;
}