#pragma once

#include <stdbool.h>
#include <stdint.h>

static inline bool pad_compute_axis_track_sizes(
    uint8_t count, uint16_t safe_size, uint16_t gap,
    const uint16_t* overrides, uint16_t* sizes)
{
    if (!count) return false;

    const uint32_t total_gap = (uint32_t)(count - 1) * gap;
    const uint16_t available = safe_size > total_gap
        ? safe_size - total_gap : 0;
    bool has_override = false;
    uint32_t fixed_total = 0;
    uint8_t auto_count = 0;

    for (uint8_t track = 0; track < count; track++) {
        const uint16_t fixed_size = overrides ? overrides[track] : 0;
        if (fixed_size) {
            has_override = true;
            fixed_total += fixed_size;
        } else {
            auto_count++;
        }
    }

    if (has_override) {
        if (!auto_count || fixed_total + auto_count > available) {
            const uint16_t uniform = available / count;
            for (uint8_t track = 0; track < count; track++) sizes[track] = uniform;
            return false;
        }

        const uint16_t auto_size = (available - fixed_total) / auto_count;
        uint8_t remainder = (available - fixed_total) % auto_count;
        for (uint8_t track = 0; track < count; track++) {
            const uint16_t fixed_size = overrides ? overrides[track] : 0;
            if (fixed_size) {
                sizes[track] = fixed_size;
            } else {
                sizes[track] = auto_size + (remainder ? 1 : 0);
                if (remainder) remainder--;
            }
        }
        return true;
    }

    const uint16_t uniform = available / count;
    for (uint8_t track = 0; track < count; track++) sizes[track] = uniform;
    return true;
}