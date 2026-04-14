#include "pad_block.h"

#if HAS_DISPLAY

// ============================================================================
// Block catalog registry
//
// Core blocks are registered via pad_block_init().
// Feature branches register additional blocks from their own init functions
// (e.g., pad_block_coffee_init()) — see pad_block.h for the pattern.
// ============================================================================

static const PadBlock* g_catalog[PAD_BLOCK_MAX_CATALOG];
static uint8_t g_catalog_count = 0;

bool pad_block_register(const PadBlock* block) {
    if (!block || g_catalog_count >= PAD_BLOCK_MAX_CATALOG) return false;
    g_catalog[g_catalog_count++] = block;
    return true;
}

const PadBlock* const* pad_block_catalog() {
    return g_catalog;
}

uint8_t pad_block_catalog_count() {
    return g_catalog_count;
}

// ============================================================================
// Countdown Timer block (3 cols × 2 rows, 5 buttons, 6 cells)
// ============================================================================

static const PadBlockButton countdown_buttons[] = {
    // Row 0: three rockers (minutes, 10-sec, seconds)
    { 0, 0, 1, 1, R"({"widget_type":"rocker","label_center":"1 min","bg_color":"#2a2a3e","actions":[{"type":"timer","timer_command":"1:adjust:60"}],"lp_actions":[{"type":"timer","timer_command":"1:adjust:-60"}]})" },
    { 1, 0, 1, 1, R"({"widget_type":"rocker","label_center":"10 sec","bg_color":"#2a2a3e","actions":[{"type":"timer","timer_command":"1:adjust:10"}],"lp_actions":[{"type":"timer","timer_command":"1:adjust:-10"}]})" },
    { 2, 0, 1, 1, R"({"widget_type":"rocker","label_center":"1 sec","bg_color":"#2a2a3e","actions":[{"type":"timer","timer_command":"1:adjust:1"}],"lp_actions":[{"type":"timer","timer_command":"1:adjust:-1"}]})" },
    // Row 1: timer display (spans 2 cols), start/pause+reset
    { 0, 1, 2, 1, R"({"label_center":"[timer:1;mm:ss]","label_center_style":"font_size:48;font_family:segment","bg_color":"#111122","fg_color":"#00FF88"})" },
    { 2, 1, 1, 1, R"json({"label_center":"[expr:[timer:1_state]==\"running\"?\"Pause\":\"Start\"]","label_bottom":"(hold to reset)","bg_color":"[expr:[timer:1_state]==\"running\"?\"#B8860B\":\"#006644\"]","actions":[{"type":"timer","timer_command":"1:toggle"}],"lp_actions":[{"type":"timer","timer_command":"1:reset"},{"type":"timer","timer_command":"1:stop"}]})json" },
};

static const PadBlock countdown_block = {
    "countdown_timer",
    "Countdown Timer",
    "Set a countdown time with rockers and start/pause/reset",
    "\xe2\x8f\xb1\xef\xb8\x8f",  // ⏱️ UTF-8
    3, 2, 6,
    5, countdown_buttons,
    0, nullptr
};

// ============================================================================
// Core block registration — called once at startup
// ============================================================================

void pad_block_init() {
    pad_block_register(&countdown_block);
}

#endif // HAS_DISPLAY
