#ifndef PAD_CYCLE_H
#define PAD_CYCLE_H

#include "board_config.h"

#include <stddef.h>
#include <stdint.h>

static_assert(MAX_PADS <= 32, "pad cycle masks require MAX_PADS <= 32");

uint32_t pad_cycle_parse_exclusions(const char* input);
void pad_cycle_format_exclusions(uint32_t mask, char* output, size_t output_len);
uint32_t pad_cycle_update_eligible_mask(uint32_t mask, uint8_t pad_index, bool eligible);
int pad_cycle_select(int current_pad, uint32_t eligible_mask,
                     uint32_t excluded_mask, int8_t direction, bool wrap);

#endif // PAD_CYCLE_H