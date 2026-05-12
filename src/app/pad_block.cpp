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
    { 0, 0, 1, 1, R"({"widget_type":"rocker","label_center":"1 min","bg_color":"#2a2a3e","actions":[{"type":"timer","timer_id":1,"timer_command":"adjust","timer_value":"60"}],"lp_actions":[{"type":"timer","timer_id":1,"timer_command":"adjust","timer_value":"-60"}]})" },
    { 1, 0, 1, 1, R"({"widget_type":"rocker","label_center":"10 sec","bg_color":"#2a2a3e","actions":[{"type":"timer","timer_id":1,"timer_command":"adjust","timer_value":"10"}],"lp_actions":[{"type":"timer","timer_id":1,"timer_command":"adjust","timer_value":"-10"}]})" },
    { 2, 0, 1, 1, R"({"widget_type":"rocker","label_center":"1 sec","bg_color":"#2a2a3e","actions":[{"type":"timer","timer_id":1,"timer_command":"adjust","timer_value":"1"}],"lp_actions":[{"type":"timer","timer_id":1,"timer_command":"adjust","timer_value":"-1"}]})" },
    // Row 1: timer display (spans 2 cols), start/pause+reset
    { 0, 1, 2, 1, R"({"label_center":"[timer:1;mm:ss]","label_center_style":"font_size:48;font_family:segment","bg_color":"#111122","fg_color":"#00FF88"})" },
    { 2, 1, 1, 1, R"json({"label_center":"[expr:[timer:1_state]==\"running\"?\"Pause\":\"Start\"]","label_bottom":"(hold to reset)","bg_color":"[expr:[timer:1_state]==\"running\"?\"#B8860B\":\"#006644\"]","actions":[{"type":"timer","timer_id":1,"timer_command":"toggle"}],"lp_actions":[{"type":"timer","timer_id":1,"timer_command":"reset"},{"type":"timer","timer_id":1,"timer_command":"stop"}]})json" },
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
// System Info block (3 cols × 3 rows, 4 buttons, 9 cells)
// ============================================================================

static const PadBlockButton sysinfo_buttons[] = {
    // Row 0: network info bar (spans 3 cols)
    { 0, 0, 3, 1, R"json({"label_top":"http://[health:hostname].local","label_center":"[health:ip]","label_bottom":"WiFi: [health:wifi_ssid] ([health:rssi] dBm)"})json" },
    // Row 1-2, col 0-1: triple gauge (CPU / RAM / PSRAM)
    { 0, 1, 2, 2, R"json({"widget_type":"gauge","widget_data_binding":"[health:cpu]","widget_data_binding_2":"[expr:([health:heap_internal]/[health:heap_internal_total]) * 100]","widget_data_binding_3":"[expr:([health:psram_free]/[health:psram_total]) * 100]","widget_gauge_start_label":"CPU: [health:cpu]%","widget_gauge_start_label_2":"RAM: [expr:[health:heap_internal]/1024;%d] KiB","widget_gauge_start_label_3":"PSRAM: [expr:[health:psram_free]/(1024*1024);%d] MiB","widget_gauge_max":100,"widget_gauge_degrees":270,"widget_gauge_start_angle":270,"widget_arc_color":"#4CAF50","widget_arc_color_2":"#2196F3","widget_arc_color_3":"#FF5722","widget_gauge_track_color":"#1A1A1A","widget_gauge_arc_width_pct":15,"widget_gauge_ticks":5,"widget_gauge_tick_color":"#808080","widget_gauge_tick_width":1,"widget_gauge_show_needle":false,"widget_anim_ms":300})json" },
    // Row 1, col 2: chip info
    { 2, 1, 1, 1, R"json({"label_top":"[health:chip]","label_center":"v[health:firmware]","label_bottom":"[health:chip_cores] core(s)"})json" },
    // Row 2, col 2: uptime
    { 2, 2, 1, 1, R"json({"label_top":"Uptime","label_center":"[expr:[health:uptime]/60;%d] min","label_bottom":"([health:reset_reason])"})json" },
};

static const PadBlock sysinfo_block = {
    "system_info",
    "System Info",
    "Network, CPU/RAM/PSRAM gauges, chip info, and uptime",
    "\xf0\x9f\x96\xa5\xef\xb8\x8f",  // 🖥️ UTF-8
    3, 3, 9,
    4, sysinfo_buttons,
    0, nullptr
};

// ============================================================================
// Core block registration — called once at startup
// ============================================================================

void pad_block_init() {
    pad_block_register(&countdown_block);
    pad_block_register(&sysinfo_block);
}

#endif // HAS_DISPLAY
