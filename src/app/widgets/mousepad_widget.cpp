#include "widget.h"

#if HAS_DISPLAY && HAS_TOUCH && HAS_USB_HID

#include "mousepad_input.h"
#include "mouse_surface_touch.h"
#include <cmath>
#include <new>

struct MousepadConfig {
    float sensitivity;
    float acceleration;
    float movement_threshold;
};

struct MousepadState {
    MousepadInput input;
    float sensitivity = 1;
    float acceleration = 0;
    float movement_threshold = MousepadInput::default_movement_threshold;
    MouseSurfaceTouch touch;
};

static_assert(sizeof(MousepadConfig) <= WIDGET_CONFIG_MAX_BYTES, "Mousepad config too large");
static_assert(sizeof(MousepadState) <= WIDGET_STATE_MAX_BYTES, "Mousepad state too large");

static void mousepad_parse(const JsonObject& btn, uint8_t* data) {
    auto* cfg = reinterpret_cast<MousepadConfig*>(data);
    const float sensitivity = btn["widget_mousepad_sensitivity"] | 1.0f;
    cfg->sensitivity = std::isfinite(sensitivity) ? clamp_val(sensitivity, 0.1f, 5.0f) : 1.0f;
    const float acceleration = btn["widget_mousepad_acceleration"] | 0.0f;
    cfg->acceleration = std::isfinite(acceleration) ? clamp_val(acceleration, 0.0f, 5.0f) : 0.0f;
    const float threshold = btn["widget_mousepad_movement_threshold"] | MousepadInput::default_movement_threshold;
    cfg->movement_threshold = std::isfinite(threshold)
        ? clamp_val(threshold, 0.0f, MousepadInput::max_movement_threshold)
        : MousepadInput::default_movement_threshold;
}

static void mousepad_event(lv_event_t* event) {
    auto* state = static_cast<MousepadState*>(lv_event_get_user_data(event));
    lv_point_t point;
    if (!state->touch.process(event, state->input, point)) return;
    int dx, dy;
    state->input.move(point.x, point.y, state->sensitivity, dx, dy,
                      lv_tick_get(), state->acceleration, state->movement_threshold);
    if (dx || dy) mouse_hid_move(dx, dy, state->touch.epoch);
    if (lv_event_get_code(event) == LV_EVENT_RELEASED && state->input.release(lv_tick_get())) {
        mouse_hid_click(state->touch.epoch);
    }
}

static void mousepad_create(lv_obj_t* tile, const WidgetConfig* cfg,
                           const ScreenButtonConfig*, const PadRect*,
                           const UIScaleInfo*, lv_obj_t*, lv_obj_t*,
                           WidgetState* state) {
    auto* mousepad = new (state->data) MousepadState{};
    mousepad->sensitivity = reinterpret_cast<const MousepadConfig*>(cfg->data)->sensitivity;
    mousepad->acceleration = reinterpret_cast<const MousepadConfig*>(cfg->data)->acceleration;
    mousepad->movement_threshold = reinterpret_cast<const MousepadConfig*>(cfg->data)->movement_threshold;
    MouseSurfaceTouch::attach(tile, mousepad_event, mousepad);
}

static void mousepad_update(lv_obj_t*, const WidgetConfig*, WidgetState*, const char*) {}
static void mousepad_tick(lv_obj_t*, const WidgetConfig*, WidgetState*) {}

static void mousepad_hide(WidgetState* state) {
    MouseSurfaceTouch::hide(reinterpret_cast<MousepadState*>(state->data)->input);
}

static void mousepad_show(WidgetState* state) {
    MouseSurfaceTouch::show(reinterpret_cast<MousepadState*>(state->data)->input);
}

static void mousepad_destroy(WidgetState* state) {
    mousepad_hide(state);
    reinterpret_cast<MousepadState*>(state->data)->~MousepadState();
}

#if HAS_MCP
static void mousepad_describe(JsonObject& out) {
    JsonArray fields = out.createNestedArray("config_fields");
    JsonObject field = fields.createNestedObject();
    field["name"] = "widget_mousepad_sensitivity";
    field["type"] = "number";
    field["desc"] = "Relative pointer sensitivity, 0.1-5 (default 1)";
    JsonObject acceleration = fields.createNestedObject();
    acceleration["name"] = "widget_mousepad_acceleration";
    acceleration["type"] = "number";
    acceleration["desc"] = "Speed-based acceleration, 0-5 (default 0/off); higher values amplify fast finger movement";
    JsonObject threshold = fields.createNestedObject();
    threshold["name"] = "widget_mousepad_movement_threshold";
    threshold["type"] = "number";
    threshold["desc"] = "Movement threshold, 0-12 device pixels (default 3); 0 removes the dead zone; lower values can cancel taps more easily";
    out["note"] = "Single-touch USB mousepad. A short stationary tap sends left click. "
                  "Requires USB keyboard transport; consumes button actions and pad swipes. "
                  "No dragging, scrolling, or multitouch.";
}
#endif

REGISTER_WIDGET_SCHEMA_LIFECYCLE(mousepad, nullptr, false);

#endif