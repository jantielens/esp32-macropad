#pragma once

#include <stdint.h>

enum WordClockWord : uint8_t {
    WORD_CLOCK_IT, WORD_CLOCK_IS, WORD_CLOCK_FIVE_MINUTES, WORD_CLOCK_TEN_MINUTES,
    WORD_CLOCK_QUARTER, WORD_CLOCK_TWENTY, WORD_CLOCK_HALF, WORD_CLOCK_PAST,
    WORD_CLOCK_TO, WORD_CLOCK_ONE, WORD_CLOCK_TWO, WORD_CLOCK_THREE,
    WORD_CLOCK_FOUR, WORD_CLOCK_FIVE_HOUR, WORD_CLOCK_SIX, WORD_CLOCK_SEVEN,
    WORD_CLOCK_EIGHT, WORD_CLOCK_NINE, WORD_CLOCK_TEN_HOUR, WORD_CLOCK_ELEVEN,
    WORD_CLOCK_TWELVE, WORD_CLOCK_OCLOCK,
};

enum WordClockMode : uint8_t {
    WORD_CLOCK_MODE_ROUNDED,
    WORD_CLOCK_MODE_MINUTE_DOTS,
    WORD_CLOCK_MODE_ACCURATE,
};

enum WordClockAccurateWord : uint8_t {
    WORD_CLOCK_ACCURATE_IT, WORD_CLOCK_ACCURATE_IS, WORD_CLOCK_ACCURATE_MINUTE,
    WORD_CLOCK_ACCURATE_MINUTE_PLURAL, WORD_CLOCK_ACCURATE_MINUTE_ONE,
    WORD_CLOCK_ACCURATE_MINUTE_TWO, WORD_CLOCK_ACCURATE_MINUTE_THREE, WORD_CLOCK_ACCURATE_MINUTE_FOUR,
    WORD_CLOCK_ACCURATE_MINUTE_FIVE, WORD_CLOCK_ACCURATE_MINUTE_SIX, WORD_CLOCK_ACCURATE_MINUTE_SEVEN,
    WORD_CLOCK_ACCURATE_MINUTE_EIGHT, WORD_CLOCK_ACCURATE_MINUTE_NINE, WORD_CLOCK_ACCURATE_MINUTE_TEN,
    WORD_CLOCK_ACCURATE_MINUTE_ELEVEN, WORD_CLOCK_ACCURATE_MINUTE_TWELVE, WORD_CLOCK_ACCURATE_THIRTEEN,
    WORD_CLOCK_ACCURATE_FOURTEEN, WORD_CLOCK_ACCURATE_FIFTEEN, WORD_CLOCK_ACCURATE_SIXTEEN,
    WORD_CLOCK_ACCURATE_SEVENTEEN, WORD_CLOCK_ACCURATE_EIGHTEEN, WORD_CLOCK_ACCURATE_NINETEEN,
    WORD_CLOCK_ACCURATE_TWENTY, WORD_CLOCK_ACCURATE_THIRTY, WORD_CLOCK_ACCURATE_FORTY,
    WORD_CLOCK_ACCURATE_FIFTY, WORD_CLOCK_ACCURATE_QUARTER, WORD_CLOCK_ACCURATE_HALF,
    WORD_CLOCK_ACCURATE_PAST, WORD_CLOCK_ACCURATE_TO, WORD_CLOCK_ACCURATE_HOUR_ONE,
    WORD_CLOCK_ACCURATE_HOUR_TWO,
    WORD_CLOCK_ACCURATE_HOUR_THREE, WORD_CLOCK_ACCURATE_HOUR_FOUR, WORD_CLOCK_ACCURATE_HOUR_FIVE,
    WORD_CLOCK_ACCURATE_HOUR_SIX, WORD_CLOCK_ACCURATE_HOUR_SEVEN, WORD_CLOCK_ACCURATE_HOUR_EIGHT,
    WORD_CLOCK_ACCURATE_HOUR_NINE, WORD_CLOCK_ACCURATE_HOUR_TEN, WORD_CLOCK_ACCURATE_HOUR_ELEVEN,
    WORD_CLOCK_ACCURATE_HOUR_TWELVE, WORD_CLOCK_ACCURATE_OCLOCK,
};

static inline uint8_t word_clock_burn_in_shift_pixels(uint8_t font_size, uint8_t configured_pixels) {
    if (configured_pixels) return configured_pixels;
    const uint8_t adaptive_pixels = static_cast<uint8_t>(font_size / 3u);
    return adaptive_pixels < 4u ? 4u : adaptive_pixels;
}

static inline int16_t word_clock_burn_in_shift_x(uint8_t phase, uint8_t pixels) {
    return static_cast<int16_t>(static_cast<int16_t>(phase % 3u) - 1) * pixels;
}

static inline int16_t word_clock_burn_in_shift_y(uint8_t phase, uint8_t pixels) {
    return static_cast<int16_t>(static_cast<int16_t>(phase / 3u) - 1) * pixels;
}

static inline uint8_t word_clock_burn_in_next_phase(uint8_t phase) {
    return static_cast<uint8_t>((phase + 1u) % 9u);
}

static inline WordClockWord word_clock_hour_word(uint8_t hour) {
    switch (hour % 12u) {
        case 0: return WORD_CLOCK_TWELVE;
        case 1: return WORD_CLOCK_ONE;
        case 2: return WORD_CLOCK_TWO;
        case 3: return WORD_CLOCK_THREE;
        case 4: return WORD_CLOCK_FOUR;
        case 5: return WORD_CLOCK_FIVE_HOUR;
        case 6: return WORD_CLOCK_SIX;
        case 7: return WORD_CLOCK_SEVEN;
        case 8: return WORD_CLOCK_EIGHT;
        case 9: return WORD_CLOCK_NINE;
        case 10: return WORD_CLOCK_TEN_HOUR;
        default: return WORD_CLOCK_ELEVEN;
    }
}

static inline uint8_t word_clock_phrase_slot(uint8_t minute, WordClockMode mode) {
    return mode == WORD_CLOCK_MODE_MINUTE_DOTS
        ? static_cast<uint8_t>(minute / 5u)
        : static_cast<uint8_t>((minute + 2u) / 5u);
}

static inline uint8_t word_clock_minute_dots(uint8_t minute, WordClockMode mode) {
    return mode == WORD_CLOCK_MODE_MINUTE_DOTS ? static_cast<uint8_t>(minute % 5u) : 0;
}

static inline bool word_clock_word_is_active(WordClockWord word, uint8_t hour, uint8_t minute,
                                               WordClockMode mode) {
    const uint8_t phrase_slot = word_clock_phrase_slot(minute, mode);
    const uint8_t rounded = static_cast<uint8_t>(phrase_slot % 12u);
    const bool to_next_hour = phrase_slot >= 7;
    const WordClockWord displayed_hour = word_clock_hour_word(
        static_cast<uint8_t>(hour + (to_next_hour ? 1u : 0u)));
    if (word == WORD_CLOCK_IT || word == WORD_CLOCK_IS) return true;
    if (rounded == 0) return word == WORD_CLOCK_OCLOCK || word == displayed_hour;
    if ((rounded == 1 || rounded == 11) && word == WORD_CLOCK_FIVE_MINUTES) return true;
    if ((rounded == 2 || rounded == 10) && word == WORD_CLOCK_TEN_MINUTES) return true;
    if ((rounded == 3 || rounded == 9) && word == WORD_CLOCK_QUARTER) return true;
    if ((rounded == 4 || rounded == 5 || rounded == 7 || rounded == 8) && word == WORD_CLOCK_TWENTY) return true;
    if ((rounded == 5 || rounded == 7) && word == WORD_CLOCK_FIVE_MINUTES) return true;
    if (rounded == 6 && word == WORD_CLOCK_HALF) return true;
    if (word == displayed_hour) return true;
    return to_next_hour ? word == WORD_CLOCK_TO : word == WORD_CLOCK_PAST;
}

static inline WordClockAccurateWord word_clock_accurate_number_word(uint8_t value) {
    return static_cast<WordClockAccurateWord>(WORD_CLOCK_ACCURATE_MINUTE_ONE + value - 1u);
}

static inline WordClockAccurateWord word_clock_accurate_tens_word(uint8_t value) {
    return static_cast<WordClockAccurateWord>(WORD_CLOCK_ACCURATE_TWENTY + value / 10u - 2u);
}

static inline WordClockAccurateWord word_clock_accurate_hour_word(uint8_t hour) {
    const uint8_t normalized_hour = static_cast<uint8_t>(hour % 12u);
    return static_cast<WordClockAccurateWord>(WORD_CLOCK_ACCURATE_HOUR_ONE +
                                              (normalized_hour ? normalized_hour - 1u : 11u));
}

static inline bool word_clock_accurate_word_is_active(WordClockAccurateWord word, uint8_t hour,
                                                       uint8_t minute, uint8_t past_threshold_minutes = 30u) {
    const bool to_next_hour = minute > past_threshold_minutes;
    const uint8_t displayed_minute = to_next_hour ? static_cast<uint8_t>(60u - minute) : minute;
    const WordClockAccurateWord displayed_hour = word_clock_accurate_hour_word(
        static_cast<uint8_t>(hour + (to_next_hour ? 1u : 0u)));
    if (word == WORD_CLOCK_ACCURATE_IT || word == WORD_CLOCK_ACCURATE_IS) return true;
    if (displayed_minute == 0u) return word == displayed_hour || word == WORD_CLOCK_ACCURATE_OCLOCK;
    if (word == displayed_hour || word == (to_next_hour ? WORD_CLOCK_ACCURATE_TO : WORD_CLOCK_ACCURATE_PAST))
        return true;
    if (displayed_minute == 15u) return word == WORD_CLOCK_ACCURATE_QUARTER;
    if (displayed_minute == 30u) return word == WORD_CLOCK_ACCURATE_HALF;
    if (word == WORD_CLOCK_ACCURATE_MINUTE) return true;
    if (word == WORD_CLOCK_ACCURATE_MINUTE_PLURAL) return displayed_minute != 1u;
    if (displayed_minute <= 19u) return word == word_clock_accurate_number_word(displayed_minute);
    if (word == word_clock_accurate_tens_word(displayed_minute)) return true;
    const uint8_t units = static_cast<uint8_t>(displayed_minute % 10u);
    return units && word == word_clock_accurate_number_word(units);
}