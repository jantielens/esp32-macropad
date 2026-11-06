#pragma once

#include "widget.h"

#if HAS_DISPLAY
struct ToggleConfig {
    char on_value[64];
    char off_value[64];
    char on_color[8];
    char off_color[8];
    char knob_color[8];
    uint8_t width_pct;
    bool read_only;
};

struct ToggleState {
    lv_obj_t* control;
    lv_obj_t* button;
    int16_t layout_width;
    int16_t layout_height;
    uint32_t pending_since;
    bool bound;
    bool value;
    bool known;
    bool available;
    bool pending;
    bool target;
    bool read_only;
};

bool toggle_interactive(const WidgetState* state);
void toggle_tap(const WidgetConfig* config, WidgetState* state,
                const ButtonAction* on_actions, uint8_t on_count,
                const ButtonAction* off_actions, uint8_t off_count);
#endif