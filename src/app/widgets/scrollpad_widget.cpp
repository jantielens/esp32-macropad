#include "widget.h"

#if HAS_DISPLAY && HAS_TOUCH && HAS_USB_HID

#include "scrollpad_input.h"
#include "mouse_surface_touch.h"
#include <cmath>
#include <new>

struct ScrollpadConfig {
    float sensitivity;
    float inertia;
    bool horizontal;
    bool reverse;
};

struct ScrollpadState {
    ScrollpadInput input;
    ScrollpadConfig config;
    MouseSurfaceTouch touch;
};

static_assert(sizeof(ScrollpadConfig) <= WIDGET_CONFIG_MAX_BYTES, "Scrollpad config too large");
static_assert(sizeof(ScrollpadState) <= WIDGET_STATE_MAX_BYTES, "Scrollpad state too large");

static void scrollpad_parse(const JsonObject& btn, uint8_t* data) {
    auto* cfg = reinterpret_cast<ScrollpadConfig*>(data);
    const float sensitivity = btn["widget_scrollpad_sensitivity"] | 1.0f;
    cfg->sensitivity = std::isfinite(sensitivity) ? clamp_val(sensitivity, 0.1f, 5.0f) : 1.0f;
    const float inertia = btn["widget_scrollpad_inertia"] | 0.0f;
    cfg->inertia = std::isfinite(inertia) ? clamp_val(inertia, 0.0f, 5.0f) : 0.0f;
    cfg->horizontal = strcmp(btn["widget_scrollpad_axis"] | "vertical", "horizontal") == 0;
    cfg->reverse = btn["widget_scrollpad_reverse"] | false;
}

static void scrollpad_event(lv_event_t* event) {
    auto* state = static_cast<ScrollpadState*>(lv_event_get_user_data(event));
    lv_point_t point;
    if (!state->touch.process(event, state->input, point)) return;
    const int steps = state->input.move(point.x, point.y, state->config.horizontal,
                                      state->config.sensitivity, state->config.reverse, lv_tick_get());
    if (steps) {
        mouse_hid_scroll(state->config.horizontal ? 0 : steps,
                         state->config.horizontal ? steps : 0, state->touch.epoch);
    }
    if (lv_event_get_code(event) == LV_EVENT_RELEASED) {
        const float velocity = state->input.release(lv_tick_get());
        mouse_hid_start_scroll_inertia(velocity, state->config.inertia,
                                      state->config.horizontal, state->touch.epoch);
    }
}

static void scrollpad_create(lv_obj_t* tile, const WidgetConfig* cfg,
                            const ScreenButtonConfig*, const PadRect*,
                            const UIScaleInfo*, lv_obj_t*, lv_obj_t*,
                            WidgetState* state) {
    auto* scrollpad = new (state->data) ScrollpadState{};
    scrollpad->config = *reinterpret_cast<const ScrollpadConfig*>(cfg->data);
    MouseSurfaceTouch::attach(tile, scrollpad_event, scrollpad);
}

static void scrollpad_update(lv_obj_t*, const WidgetConfig*, WidgetState*, const char*) {}
static void scrollpad_tick(lv_obj_t*, const WidgetConfig*, WidgetState*) {}

static void scrollpad_show(WidgetState* state) {
    MouseSurfaceTouch::show(reinterpret_cast<ScrollpadState*>(state->data)->input);
}

static void scrollpad_hide(WidgetState* state) {
    MouseSurfaceTouch::hide(reinterpret_cast<ScrollpadState*>(state->data)->input);
}

static void scrollpad_destroy(WidgetState* state) {
    scrollpad_hide(state);
    reinterpret_cast<ScrollpadState*>(state->data)->~ScrollpadState();
}

#if HAS_MCP
static void scrollpad_describe(JsonObject& out) {
    JsonArray fields = out.createNestedArray("config_fields");
    JsonObject axis = fields.createNestedObject();
    axis["name"] = "widget_scrollpad_axis";
    axis["type"] = "string";
    axis["desc"] = "vertical (default) or horizontal scrolling; perpendicular movement is ignored";
    JsonObject sensitivity = fields.createNestedObject();
    sensitivity["name"] = "widget_scrollpad_sensitivity";
    sensitivity["type"] = "number";
    sensitivity["desc"] = "Wheel sensitivity, 0.1-5 (default 1); one step per 20 device pixels at 1";
    JsonObject reverse = fields.createNestedObject();
    reverse["name"] = "widget_scrollpad_reverse";
    reverse["type"] = "boolean";
    reverse["desc"] = "Reverse scroll direction (default false); normally finger up scrolls up and right scrolls right";
    JsonObject inertia = fields.createNestedObject();
    inertia["name"] = "widget_scrollpad_inertia";
    inertia["type"] = "number";
    inertia["desc"] = "Release coasting, 0-5 (default 0/off); higher values coast longer; either mouse surface stops coasting on touch";
    out["note"] = "Single-touch USB scroll surface. Requires USB keyboard transport; "
                  "consumes button actions and pad swipes and retains touches outside its bounds. "
                  "No clicks, pointer movement, or BLE mouse. Optional inertia stops on touch, hide, reconnect, or OTA. "
                  "Horizontal support depends on the host application.";
}
#endif

REGISTER_WIDGET_SCHEMA_LIFECYCLE(scrollpad, nullptr, false);

#endif