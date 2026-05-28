// ============================================================================
// Host-side size regression guard for ButtonAction
// ============================================================================
// Prints sizeof(ButtonAction) and every per-type payload arm.
// Asserts: sizeof(ButtonAction) <= ACTION_SIZEOF_BUDGET.
//
// Update the budget intentionally (with measurement notes) when adding a new
// per-class payload. The intent is: shared embedded boards (macropad, e-paper,
// headless) pay exactly the same per-action memory cost regardless of which
// device-class flags are set. This file is the canary.
// ============================================================================

#include "pad_config.h"
#include <cassert>
#include <cstdio>

// Tightened in Phase 1 once the union lands. Pre-union baseline is whatever
// the flat struct happens to be — this run records it.
#ifndef ACTION_SIZEOF_BUDGET
#define ACTION_SIZEOF_BUDGET 1024
#endif

int main() {
    printf("=== Action size report ===\n");
    printf("sizeof(ButtonAction)              = %zu bytes\n", sizeof(ButtonAction));
#ifdef ACTION_PAYLOAD_PRESENT
    printf("sizeof(ActionPayload)             = %zu bytes\n", sizeof(ActionPayload));
#  ifdef ACTION_PAYLOAD_DUMP_ARMS
    ACTION_PAYLOAD_DUMP_ARMS
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
