#include "pad_config.h"

#include <assert.h>
#include <stdio.h>

int main() {
    // PadConfig stores a pointer to only the configured buttons. A fixed
    // ScreenButtonConfig[MAX_PAD_BUTTONS] member would exceed this bound.
    static_assert(sizeof(PadConfig) < 16 * 1024,
                  "PadConfig must not embed the maximum button array");

    PadConfig config = {};
    assert(config.buttons == nullptr);
    assert(config.button_capacity == 0);
    puts("pad_config_dynamic_layout: PASS");
    return 0;
}