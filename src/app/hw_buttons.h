#pragma once

#include "board_config.h"

// ============================================================================
// Hardware Button Actions — interrupt-driven GPIO button dispatcher
// ============================================================================
// Detects tap (release before hold threshold) and hold (held past threshold)
// events on board-declared GPIO buttons (HW_BUTTON_DEFS) and dispatches the
// user-configured action lists via action_list_dispatch().
//
// Works independently of HAS_DISPLAY — gated only on HAS_BUTTON. The module
// must NEVER include any display/LVGL header; display-dependent action types
// are guarded inside action_dispatch.cpp, not here.

#if HAS_BUTTON

// Initialize GPIO pins + interrupts. Call from setup() AFTER
// check_config_mode_button() completes and after config is loaded.
void hw_buttons_init();

// Process debounce + hold detection and dispatch actions. Call from loop().
void hw_buttons_loop();

// Update a board-defined virtual button from a non-GPIO input source.
void hw_buttons_set_virtual_state(uint8_t index, bool pressed);

#else  // !HAS_BUTTON — zero-cost no-op stubs

static inline void hw_buttons_init() {}
static inline void hw_buttons_loop() {}
static inline void hw_buttons_set_virtual_state(uint8_t, bool) {}

#endif  // HAS_BUTTON
