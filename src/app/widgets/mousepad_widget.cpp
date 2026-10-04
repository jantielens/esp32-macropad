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
    float inertia;
    bool reverse;
};

struct MousepadState {
    MousepadInput input;
    float sensitivity = 1;
    float acceleration = 0;
    float movement_threshold = MousepadInput::default_movement_threshold;
    float inertia = 0;
    bool reverse = false;
    MouseSurfaceTouch touch;
    uint32_t owner = 0;
};

static_assert(sizeof(MousepadConfig) <= WIDGET_CONFIG_MAX_BYTES, "Mousepad config too large");
static_assert(sizeof(MousepadState) <= WIDGET_STATE_MAX_BYTES, "Mousepad state too large");

static const char* mousepad_validate(JsonObjectConst button) {
    struct Range { const char* key; float minimum; float maximum; };
    const Range ranges[] = {
        {"widget_mousepad_sensitivity", 0.1f, 5.0f},
        {"widget_mousepad_acceleration", 0.0f, 5.0f},
        {"widget_mousepad_movement_threshold", 0.0f, MousepadInput::max_movement_threshold},
        {"widget_mousepad_inertia", 0.0f, 5.0f}
    };
    for (const auto& range : ranges) {
        if (!button.containsKey(range.key)) continue;
        const JsonVariantConst value = button[range.key];
        if (!value.is<float>() || !std::isfinite(value.as<float>()) ||
            value.as<float>() < range.minimum || value.as<float>() > range.maximum)
            return "mousepad numbers must be finite and within their documented ranges";
    }
    if (button.containsKey("widget_mousepad_reverse") && !button["widget_mousepad_reverse"].is<bool>())
        return "widget_mousepad_reverse must be boolean";
    return nullptr;
}

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
    const float inertia = btn["widget_mousepad_inertia"] | 0.0f;
    cfg->inertia = std::isfinite(inertia) ? clamp_val(inertia, 0.0f, 5.0f) : 0.0f;
    cfg->reverse = btn["widget_mousepad_reverse"] | false;
}

static void mousepad_point(void* context, MouseTouchEvent interaction, uint8_t id, const lv_point_t& point) {
    auto* state = static_cast<MousepadState*>(context);
    const uint32_t now = lv_tick_get();
    MousepadInput::Output output;
    if (interaction == MouseTouchEvent::Cancel) {
        state->input.cancel();
        mouse_hid_release(state->owner, state->touch.epoch);
        state->owner = 0;
    }
    else if (interaction == MouseTouchEvent::End) state->input.end_session();
    else if (interaction == MouseTouchEvent::Press) state->input.contact_press(id, point.x, point.y, now);
    else if (interaction == MouseTouchEvent::Position) state->input.contact_position(id, point.x, point.y);
    else if (interaction == MouseTouchEvent::Move)
        output = state->input.contact_move(id, point.x, point.y, now, state->sensitivity,
                                          state->acceleration, state->movement_threshold);
    else if (interaction == MouseTouchEvent::Sample)
        output = state->input.scroll_sample(now, state->sensitivity, state->movement_threshold, state->reverse);
    else if (interaction == MouseTouchEvent::Release) output = state->input.contact_release(id, now);
    if (output.drag_start) {
        state->owner = mouse_hid_acquire(state->touch.epoch);
        if (!state->owner) { state->touch.cancel(); return; }
    }
    if (output.dx || output.dy) mouse_hid_move(output.dx, output.dy, state->touch.epoch);
    if (output.wheel || output.pan) mouse_hid_scroll(output.wheel, output.pan, state->touch.epoch);
    if (output.click && !mouse_hid_click(state->touch.epoch)) state->input.cancel();
    if (output.drag_end) {
        mouse_hid_release(state->owner, state->touch.epoch);
        state->owner = 0;
    }
    if (output.velocity) mouse_hid_start_scroll_inertia(output.velocity, state->inertia,
                                                       output.horizontal, state->touch.epoch);
}

static void mousepad_event(lv_event_t* event) {
    static_cast<MousepadState*>(lv_event_get_user_data(event))->touch.process(event);
}

static void mousepad_create(lv_obj_t* tile, const WidgetConfig* cfg,
                           const ScreenButtonConfig*, const PadRect*,
                           const UIScaleInfo*, lv_obj_t*, lv_obj_t*,
                           WidgetState* state) {
    auto* mousepad = new (state->data) MousepadState{};
    mousepad->sensitivity = reinterpret_cast<const MousepadConfig*>(cfg->data)->sensitivity;
    mousepad->acceleration = reinterpret_cast<const MousepadConfig*>(cfg->data)->acceleration;
    mousepad->movement_threshold = reinterpret_cast<const MousepadConfig*>(cfg->data)->movement_threshold;
    mousepad->inertia = reinterpret_cast<const MousepadConfig*>(cfg->data)->inertia;
    mousepad->reverse = reinterpret_cast<const MousepadConfig*>(cfg->data)->reverse;
    mousepad->touch.attach(tile, mousepad_event, mousepad, mousepad_point);
}

static void mousepad_update(lv_obj_t*, const WidgetConfig*, WidgetState*, const char*) {}
static void mousepad_tick(lv_obj_t*, const WidgetConfig*, WidgetState*) {}

static void mousepad_hide(WidgetState* state) {
    reinterpret_cast<MousepadState*>(state->data)->touch.hide();
}

static void mousepad_show(WidgetState* state) {
    reinterpret_cast<MousepadState*>(state->data)->touch.show();
}

static void mousepad_destroy(WidgetState* state) {
    reinterpret_cast<MousepadState*>(state->data)->touch.detach(mousepad_event);
    reinterpret_cast<MousepadState*>(state->data)->~MousepadState();
}

#if HAS_MCP
static void mousepad_describe(JsonObject& out) {
    JsonArray fields = out.createNestedArray("config_fields");
    JsonObject field = fields.createNestedObject();
    field["name"] = "widget_mousepad_sensitivity";
    field["type"] = "number";
    field["desc"] = "Pointer and two-finger scroll sensitivity, 0.1-5 (default 1); one wheel step per 20 midpoint pixels at 1";
    JsonObject acceleration = fields.createNestedObject();
    acceleration["name"] = "widget_mousepad_acceleration";
    acceleration["type"] = "number";
    acceleration["desc"] = "Speed-based acceleration, 0-5 (default 0/off); higher values amplify fast finger movement";
    JsonObject threshold = fields.createNestedObject();
    threshold["name"] = "widget_mousepad_movement_threshold";
    threshold["type"] = "number";
    threshold["desc"] = "Pointer, drag, and midpoint scroll activation threshold, 0-12 device pixels (default 3); 0 removes the dead zone";
    JsonObject reverse = fields.createNestedObject();
    reverse["name"] = "widget_mousepad_reverse";
    reverse["type"] = "boolean";
    reverse["desc"] = "Reverse two-finger scrolling (default false); normally finger up scrolls up and right scrolls right";
    JsonObject inertia = fields.createNestedObject();
    inertia["name"] = "widget_mousepad_inertia";
    inertia["type"] = "number";
    inertia["desc"] = "Scroll release coasting, 0-5 (default 0/off); uses scrollpad conventions and stops on mouse-surface touch or cancellation";
    out["note"] = "USB mousepad: one finger moves; a short stationary tap sends left click. "
                  "Tap then touch within 300 ms and move to drag; lift the owning finger to release. "
                  "A stationary second tap double-clicks. Two contacts starting in this mousepad scroll "
                  "by midpoint with dominant-axis lock (ties vertical); lift either to stop and lift all before restarting. "
                  "Extra fingers never replace captured contacts or change a drag into scrolling. "
                  "Requires USB keyboard transport; consumes button actions and pad swipes. "
                  "Single-contact drivers retain movement, taps, and dragging; use scrollpad for one-finger scrolling. "
                  "No BLE mouse, drag lock, pinch, or multi-finger clicks.";
}
#endif

REGISTER_WIDGET_SCHEMA_VALIDATED_LIFECYCLE(mousepad, nullptr, false, mousepad_validate);

#endif