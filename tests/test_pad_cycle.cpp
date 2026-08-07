#include "pad_cycle.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(actual, expected) do { \
    int actual_value = (actual); \
    int expected_value = (expected); \
    if (actual_value != expected_value) { \
        printf("FAIL line %d: got %d, expected %d\n", __LINE__, actual_value, expected_value); \
        failures++; \
    } \
} while (0)

#define CHECK_MASK(actual, expected) do { \
    uint32_t actual_value = (actual); \
    uint32_t expected_value = (expected); \
    if (actual_value != expected_value) { \
        printf("FAIL line %d: got 0x%08x, expected 0x%08x\n", __LINE__, \
               (unsigned)actual_value, (unsigned)expected_value); \
        failures++; \
    } \
} while (0)

int main() {
    const uint32_t sparse = (1u << 0) | (1u << 2) | (1u << 5);
    CHECK(pad_cycle_select(0, sparse, 0, 1, true), 2);
    CHECK(pad_cycle_select(5, sparse, 0, -1, true), 2);
    CHECK(pad_cycle_select(5, sparse, 0, 1, true), 0);
    CHECK(pad_cycle_select(5, sparse, 0, 1, false), -1);
    CHECK(pad_cycle_select(0, sparse, 0, -1, false), -1);
    CHECK(pad_cycle_select(2, sparse, 1u << 2, 1, true), 5);
    CHECK(pad_cycle_select(1, sparse, 0, 1, true), 2);
    CHECK(pad_cycle_select(-1, sparse, 0, 1, false), 0);
    CHECK(pad_cycle_select(-1, sparse, 0, -1, false), 5);
    CHECK(pad_cycle_select(0, 0, 0, 1, true), -1);
    CHECK(pad_cycle_select(2, 1u << 2, 0, 1, true), -1);
    CHECK(pad_cycle_select(-1, 1u << 2, 0, 1, false), 2);
    CHECK(pad_cycle_select(0, sparse, sparse, 1, true), -1);

    CHECK_MASK(pad_cycle_parse_exclusions(""), 0);
    CHECK_MASK(pad_cycle_parse_exclusions(" 1, 5,1 "), (1u << 0) | (1u << 4));
    CHECK_MASK(pad_cycle_parse_exclusions("5, 1,5,bad,99,+2,-3"),
               (1u << 0) | (1u << 4));

    char formatted[64];
    pad_cycle_format_exclusions((1u << 4) | (1u << 0), formatted, sizeof(formatted));
    if (strcmp(formatted, "1,5") != 0) {
        printf("FAIL canonical format: got '%s'\n", formatted);
        failures++;
    }

    uint32_t mask = pad_cycle_update_eligible_mask(0, 3, true);
    CHECK_MASK(mask, 1u << 3);
    mask = pad_cycle_update_eligible_mask(mask, 3, false);
    CHECK_MASK(mask, 0);
    mask = pad_cycle_update_eligible_mask(1u << 1, 1, true);
    CHECK_MASK(mask, 1u << 1);
    mask = pad_cycle_update_eligible_mask(mask, 4, true);
    CHECK_MASK(mask, (1u << 1) | (1u << 4));

    printf("pad_cycle: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}