// ============================================================================
// Host-side size regression guard for ButtonAction
// ============================================================================
// Prints the typed action storage size.
// Asserts: sizeof(ButtonAction) <= ACTION_SIZEOF_BUDGET.
//
// The budget allows two pointers and the discriminator on a 64-bit host.
// ============================================================================

#include "pad_config.h"
#include <cassert>
#include <cstdio>

#ifndef ACTION_SIZEOF_BUDGET
#define ACTION_SIZEOF_BUDGET 32
#endif

int main() {
    printf("=== Action size report ===\n");
    printf("sizeof(ButtonAction)              = %zu bytes\n", sizeof(ButtonAction));
#ifdef ACTION_PAYLOAD_PRESENT
    printf("sizeof(ActionPayload)             = %zu bytes\n", sizeof(ActionPayload));
#  ifdef ACTION_PAYLOAD_DUMP_ARMS
    ACTION_PAYLOAD_DUMP_ARMS(printf);
#  endif
#endif
    printf("Budget                            = %u bytes\n",
           (unsigned)ACTION_SIZEOF_BUDGET);

    if (sizeof(ButtonAction) > ACTION_SIZEOF_BUDGET) {
        printf("FAIL: sizeof(ButtonAction)=%zu exceeds budget %u\n",
               sizeof(ButtonAction), (unsigned)ACTION_SIZEOF_BUDGET);
        return 1;
    }
    printf("OK\n");
    return 0;
}
