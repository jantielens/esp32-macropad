#include "widget.h"

#if HAS_DISPLAY && HAS_TOUCH && HAS_USB_HID

#include "mousepad_input.h"
#include "mouse_surface_touch.h"
#include <cmath>
#include <new>

extern bool display_manager_go_back();

struct MousepadConfig {
    float sensitivity;
    float acceleration;
    float movement_threshold;
    float inertia;
    bool reverse;
    bool button_zones_enabled;
    bool show_back;
};

struct MousepadState {
    MousepadInput input;
    float sensitivity = 1;
    float acceleration = 0;
    float movement_threshold = MousepadInput::default_movement_threshold;
    float inertia = 0;
    bool reverse = false;
    bool button_zones_enabled = false;
    MouseSurfaceTouch touch;
    uint32_t owner = 0;
    lv_point_t back_start{};
    bool back_tap = false;
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
    if (button.containsKey("widget_mousepad_buttons") && !button["widget_mousepad_buttons"].is<bool>())
        return "widget_mousepad_buttons must be boolean";
    if (button.containsKey("widget_mousepad_back") && !button["widget_mousepad_back"].is<bool>())
        return "widget_mousepad_back must be boolean";
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
    cfg->button_zones_enabled = btn["widget_mousepad_buttons"] | false;
    cfg->show_back = btn["widget_mousepad_back"] | false;
}

static void mousepad_back_event(lv_event_t* event) {
    const lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_INDEV_RESET && !MouseSurfaceTouch::is_pointer_event(code)) return;
    lv_event_stop_bubbling(event);
    lv_event_stop_processing(event);
    auto* state = static_cast<MousepadState*>(lv_event_get_user_data(event));
    lv_indev_t* indev = lv_indev_active();
    if (code == LV_EVENT_PRESSED && indev) {
        state->input.cancel();
        mouse_hid_release(state->owner, state->touch.epoch);
        state->owner = 0;
        mouse_hid_begin_touch();
        lv_indev_get_point(indev, &state->back_start);
        state->back_tap = true;
    } else if ((code == LV_EVENT_PRESSING || code == LV_EVENT_RELEASED) && indev) {
        lv_point_t point;
        lv_indev_get_point(indev, &point);
        if (std::abs(point.x - state->back_start.x) > 8 ||
            std::abs(point.y - state->back_start.y) > 8) state->back_tap = false;
    } else if (code == LV_EVENT_SHORT_CLICKED) {
        const bool navigate = state->back_tap;
        state->back_tap = false;
        if (navigate) display_manager_go_back();
    } else if (code == LV_EVENT_PRESS_LOST || code == LV_EVENT_INDEV_RESET ||
               code == LV_EVENT_LONG_PRESSED) state->back_tap = false;
}

static void mousepad_point(void* context, MouseTouchEvent interaction, uint8_t id, const lv_point_t& supplied_point) {
    auto* state = static_cast<MousepadState*>(context);
    lv_point_t point = supplied_point;
    if (state->touch.object) {
        lv_area_t bounds;
        lv_obj_get_coords(state->touch.object, &bounds);
        point.x -= bounds.x1;
        point.y -= bounds.y1;
    }
    const uint32_t now = lv_tick_get();
    const uint8_t previous_button = state->input.held_button();
    MousepadInput::Output output;
    if (interaction == MouseTouchEvent::Cancel) {
        state->input.cancel();
        mouse_hid_release(state->owner, state->touch.epoch);
        state->owner = 0;
    }
    else if (interaction == MouseTouchEvent::End) state->input.end_session();
    else if (interaction == MouseTouchEvent::Press) {
        state->input.configure_buttons(state->button_zones_enabled, lv_obj_get_width(state->touch.object),
                                       lv_obj_get_height(state->touch.object));
        output = state->input.contact_press(id, point.x, point.y, now);
    }
    else if (interaction == MouseTouchEvent::Position) state->input.contact_position(id, point.x, point.y);
    else if (interaction == MouseTouchEvent::Move)
        output = state->input.contact_move(id, point.x, point.y, now, state->sensitivity,
                                          state->acceleration, state->movement_threshold);
    else if (interaction == MouseTouchEvent::Sample)
        output = state->input.scroll_sample(now, state->sensitivity, state->movement_threshold, state->reverse);
    else if (interaction == MouseTouchEvent::Release) output = state->input.contact_release(id, now);
    if (output.hold_start) {
        state->owner = mouse_hid_acquire(state->touch.epoch, output.button_mask);
        if (!state->owner) { state->touch.cancel(); return; }
    }
    if (output.dx || output.dy) mouse_hid_move(output.dx, output.dy, state->touch.epoch);
    if (output.wheel || output.pan) mouse_hid_scroll(output.wheel, output.pan, state->touch.epoch);
    if (output.click && !mouse_hid_click(state->touch.epoch)) state->input.cancel();
    if (output.hold_end) {
        mouse_hid_release(state->owner, state->touch.epoch);
        state->owner = 0;
    }
    if (output.velocity) mouse_hid_start_scroll_inertia(output.velocity, state->inertia,
                                                       output.horizontal, state->touch.epoch);
    state->touch.allow_replacement = state->input.button_held();
    if (previous_button != state->input.held_button() && state->touch.object)
        lv_obj_invalidate(state->touch.object);
}

static void mousepad_outline(lv_layer_t* layer, const lv_area_t& area, lv_color_t color, bool held) {
    const int radius = clamp_val<int>(6, 0, std::min(lv_area_get_width(&area), lv_area_get_height(&area)) / 2);
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = color;
    line.width = held ? 3 : 1;
    line.dash_width = 6;
    line.dash_gap = 4;
    const lv_point_t edges[8] = {
        {area.x1 + radius, area.y1}, {area.x2 - radius, area.y1},
        {area.x2, area.y1 + radius}, {area.x2, area.y2 - radius},
        {area.x2 - radius, area.y2}, {area.x1 + radius, area.y2},
        {area.x1, area.y2 - radius}, {area.x1, area.y1 + radius}
    };
    for (unsigned index = 0; index < 8; index += 2) {
        line.p1 = {lv_value_precise_t(edges[index].x), lv_value_precise_t(edges[index].y)};
        line.p2 = {lv_value_precise_t(edges[index + 1].x), lv_value_precise_t(edges[index + 1].y)};
        lv_draw_line(layer, &line);
    }
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.color = color;
    arc.width = line.width;
    arc.radius = radius;
    const lv_point_t centers[4] = {
        {area.x2 - radius, area.y2 - radius}, {area.x1 + radius, area.y2 - radius},
        {area.x1 + radius, area.y1 + radius}, {area.x2 - radius, area.y1 + radius}
    };
    for (unsigned corner = 0; corner < 4; ++corner) {
        arc.center = centers[corner];
        arc.start_angle = corner * 90 + 15;
        arc.end_angle = arc.start_angle + 60;
        lv_draw_arc(layer, &arc);
    }
}

static void mousepad_event(lv_event_t* event) {
    auto* state = static_cast<MousepadState*>(lv_event_get_user_data(event));
    if (lv_event_get_code(event) == LV_EVENT_DRAW_MAIN && state->button_zones_enabled) {
        lv_area_t bounds;
        lv_obj_get_coords(state->touch.object, &bounds);
        const MousepadInput::ButtonZones zones{lv_area_get_width(&bounds), lv_area_get_height(&bounds)};
        for (unsigned side = 0; side < 2; ++side) {
            lv_area_t area = bounds;
            area.x1 = bounds.x1 + zones.left(side != 0);
            area.x2 = bounds.x1 + zones.right(side != 0);
            area.y1 = bounds.y1 + zones.top();
            lv_area_increase(&area, -3, -3);
            if (lv_area_get_width(&area) < 4 || lv_area_get_height(&area) < 4) continue;
            const uint8_t mask = side ? mouse_hid_buttons::right : mouse_hid_buttons::left;
            mousepad_outline(lv_event_get_layer(event), area,
                             lv_obj_get_style_text_color(state->touch.object, LV_PART_MAIN),
                             state->input.held_button() == mask);
        }
    } else state->touch.process(event);
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
    mousepad->button_zones_enabled = reinterpret_cast<const MousepadConfig*>(cfg->data)->button_zones_enabled;
    mousepad->touch.attach(tile, mousepad_event, mousepad, mousepad_point);
    if (reinterpret_cast<const MousepadConfig*>(cfg->data)->show_back) {
        lv_obj_update_layout(tile);
        lv_obj_t* back = lv_obj_create(tile);
        const int size = clamp_val<int>(std::min(lv_obj_get_content_width(tile), lv_obj_get_content_height(tile)) - 8, 1, 44);
        lv_obj_set_size(back, size, size);
        lv_obj_set_style_pad_all(back, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(back, 6, LV_PART_MAIN);
        lv_obj_align(back, LV_ALIGN_TOP_LEFT, 4, 4);
        lv_obj_remove_flag(back, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(back, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(back, LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_add_event_cb(back, mousepad_back_event, LV_EVENT_ALL, mousepad);
        lv_obj_t* arrow = lv_label_create(back);
        lv_label_set_text(arrow, LV_SYMBOL_LEFT);
        lv_obj_center(arrow);
    }
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
    JsonObject buttons = fields.createNestedObject();
    buttons["name"] = "widget_mousepad_buttons";
    buttons["type"] = "boolean";
    buttons["desc"] = "Optional left/right mouse buttons in the bottom 20% (default false); hold one while moving or repositioning another finger above";
    JsonObject back = fields.createNestedObject();
    back["name"] = "widget_mousepad_back";
    back["type"] = "boolean";
    back["desc"] = "Show a top-left Back button isolated from mouse input (default false); uses screen history, with no fallback when empty";
    out["note"] = "USB mousepad: one finger moves; a short stationary tap sends left click. "
                  "Tap then touch within 300 ms and move to drag; lift the owning finger to release. "
                  "A stationary second tap double-clicks. Two contacts starting in this mousepad scroll "
                  "by midpoint with dominant-axis lock (ties vertical); lift either to stop and lift all before restarting. "
                  "Optional bottom button zones hold left/right on touch until lift; pointer contacts can lift and restart while a zone is held. "
                  "Contact roles are fixed at touchdown; scrolling requires two contacts above the button strip. "
                  "Extra fingers never change a drag into scrolling; only one mouse button can be held at a time. "
                  "Requires USB keyboard transport; consumes button actions and pad swipes. "
                  "Single-contact drivers retain movement, taps, and dragging; use scrollpad for one-finger scrolling. "
                  "No BLE mouse, drag lock, pinch, or multi-finger clicks.";
}
#endif

static const WidgetPreview mousepad_preview = {"Mousepad", "touchpad_mouse"};
REGISTER_WIDGET_SCHEMA_VALIDATED_LIFECYCLE(mousepad, nullptr, false, mousepad_validate);

#endif