#include "pad_cycle.h"

#include <ctype.h>
#include <stdio.h>

static uint32_t valid_pad_mask() {
    return MAX_PADS == 32 ? UINT32_MAX : ((uint32_t)1U << MAX_PADS) - 1U;
}

uint32_t pad_cycle_parse_exclusions(const char* input) {
    uint32_t mask = 0;
    const char* cursor = input;
    while (cursor && *cursor) {
        const char* token_end = cursor;
        while (*token_end && *token_end != ',') token_end++;

        const char* start = cursor;
        while (start < token_end && isspace((unsigned char)*start)) start++;
        const char* end = token_end;
        while (end > start && isspace((unsigned char)end[-1])) end--;

        uint32_t value = 0;
        bool valid = start < end;
        for (const char* character = start; valid && character < end; character++) {
            if (!isdigit((unsigned char)*character)) {
                valid = false;
                break;
            }
            value = value * 10U + (uint32_t)(*character - '0');
            if (value > MAX_PADS) valid = false;
        }
        if (valid && value >= 1U) mask |= (uint32_t)1U << (value - 1U);

        cursor = *token_end ? token_end + 1 : token_end;
    }
    return mask;
}

void pad_cycle_format_exclusions(uint32_t mask, char* output, size_t output_len) {
    if (!output || output_len == 0) return;
    output[0] = '\0';
    size_t used = 0;
    mask &= valid_pad_mask();
    for (uint8_t index = 0; index < MAX_PADS; index++) {
        if (!(mask & ((uint32_t)1U << index))) continue;
        int written = snprintf(output + used, output_len - used,
                               used ? ",%u" : "%u", (unsigned)index + 1U);
        if (written < 0 || (size_t)written >= output_len - used) {
            output[output_len - 1] = '\0';
            return;
        }
        used += (size_t)written;
    }
}

uint32_t pad_cycle_update_eligible_mask(uint32_t mask, uint8_t pad_index, bool eligible) {
    if (pad_index >= MAX_PADS) return mask & valid_pad_mask();
    uint32_t bit = (uint32_t)1U << pad_index;
    return (eligible ? (mask | bit) : (mask & ~bit)) & valid_pad_mask();
}

int pad_cycle_select(int current_pad, uint32_t eligible_mask,
                     uint32_t excluded_mask, int8_t direction, bool wrap) {
    uint32_t candidates = eligible_mask & ~excluded_mask & valid_pad_mask();
    if (!candidates) return -1;

    int step = direction < 0 ? -1 : 1;
    if (current_pad < 0 || current_pad >= MAX_PADS) {
        int index = step > 0 ? 0 : MAX_PADS - 1;
        for (; index >= 0 && index < MAX_PADS; index += step) {
            if (candidates & ((uint32_t)1U << index)) return index;
        }
        return -1;
    }

    int index = current_pad + step;
    for (uint8_t visited = 0; visited < MAX_PADS - 1; visited++) {
        if (index < 0 || index >= MAX_PADS) {
            if (!wrap) return -1;
            index = step > 0 ? 0 : MAX_PADS - 1;
        }
        if (index != current_pad && (candidates & ((uint32_t)1U << index))) return index;
        index += step;
    }
    return -1;
}