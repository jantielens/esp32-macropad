#include "widget.h"

#if HAS_DISPLAY && HAS_TOUCH && HAS_USB_HID
#include "gamepad_joystick_input.h"
#include "gamepad_surface_touch.h"
#include <new>

struct GamepadJoystickConfig {
    float dead_zone = .1f;
    uint8_t stick = 0;
    bool floating = false;
    bool invert_x = false;
    bool invert_y = false;
};
struct GamepadJoystickState {
    GamepadSurfaceTouch touch;
    GamepadJoystickInput input;
    GamepadJoystickConfig config;
    lv_obj_t* button = nullptr;
    lv_obj_t* base = nullptr;
    lv_obj_t* thumb = nullptr;
    lv_color_t foreground{};
    bool foreground_initialized = false;
    int base_diameter = -1;
    int thumb_diameter = -1;
    lv_point_t base_position{};
    lv_point_t thumb_position{};
    bool geometry_initialized = false;
    uint32_t last_axis_log = 0;
    int16_t logged_horizontal = 0;
    int16_t logged_vertical = 0;
    bool axis_logged = false;
};
static_assert(sizeof(GamepadJoystickConfig) <= WIDGET_CONFIG_MAX_BYTES, "Gamepad joystick config too large");
static_assert(sizeof(GamepadJoystickState) <= WIDGET_STATE_MAX_BYTES, "Gamepad joystick state too large");
static_assert(sizeof("gamepad_stick") <= CONFIG_WIDGET_TYPE_MAX_LEN, "Gamepad stick type name too long");

static const char* gamepad_stick_validate(JsonObjectConst button) {
    if (button.containsKey("widget_gamepad_stick") && (!button["widget_gamepad_stick"].is<const char*>() ||
        (strcmp(button["widget_gamepad_stick"].as<const char*>(), "left") != 0 && strcmp(button["widget_gamepad_stick"].as<const char*>(), "right") != 0)))
        return "widget_gamepad_stick must be left or right";
    if (button.containsKey("widget_gamepad_center") && (!button["widget_gamepad_center"].is<const char*>() ||
        (strcmp(button["widget_gamepad_center"].as<const char*>(), "fixed") != 0 && strcmp(button["widget_gamepad_center"].as<const char*>(), "floating") != 0)))
        return "widget_gamepad_center must be fixed or floating";
    if (button.containsKey("widget_gamepad_dead_zone") && (!button["widget_gamepad_dead_zone"].is<float>() ||
        !std::isfinite(button["widget_gamepad_dead_zone"].as<float>()) || button["widget_gamepad_dead_zone"].as<float>() < 0 || button["widget_gamepad_dead_zone"].as<float>() > .9f))
        return "widget_gamepad_dead_zone must be a number from 0 to 0.9";
    for (const char* key : {"widget_gamepad_invert_x", "widget_gamepad_invert_y"}) {
        if (button.containsKey(key) && !button[key].is<bool>()) return "gamepad axis inversion must be boolean";
    }
    return nullptr;
}

static void gamepad_stick_parse(const JsonObject& button, uint8_t* data) {
    auto* config = new (data) GamepadJoystickConfig{};
    config->stick = strcmp(button["widget_gamepad_stick"] | "left", "right") == 0 ? 1 : 0;
    config->floating = strcmp(button["widget_gamepad_center"] | "fixed", "floating") == 0;
    const float dead_zone = button["widget_gamepad_dead_zone"] | .1f;
    config->dead_zone = std::isfinite(dead_zone) ? clamp_val(dead_zone, 0.0f, .9f) : .1f;
    config->invert_x = button["widget_gamepad_invert_x"] | false;
    config->invert_y = button["widget_gamepad_invert_y"] | false;
}

static void gamepad_joystick_color(GamepadJoystickState* state) {
    const lv_color_t foreground = lv_obj_get_style_text_color(state->button, LV_PART_MAIN);
    if (state->foreground_initialized && lv_color_eq(state->foreground, foreground)) return;
    state->foreground = foreground;
    state->foreground_initialized = true;
    lv_obj_set_style_border_color(state->base, foreground, LV_PART_MAIN);
    lv_obj_set_style_bg_color(state->thumb, foreground, LV_PART_MAIN);
}

static void gamepad_joystick_render(GamepadJoystickState* state) {
    lv_area_t area;
    lv_obj_get_content_coords(state->button, &area);
    const int diameter = int(std::lround(state->input.base_radius * 2));
    const int thumb_size = int(std::lround(state->input.thumb_radius * 2));
    const lv_point_t base_position = {int(state->input.center_x - area.x1 - state->input.base_radius),
                                     int(state->input.center_y - area.y1 - state->input.base_radius)};
    const lv_point_t thumb_position = {int(state->input.thumb_x - area.x1 - state->input.thumb_radius),
                                      int(state->input.thumb_y - area.y1 - state->input.thumb_radius)};
    if (state->base_diameter != diameter) {
        lv_obj_set_size(state->base, diameter, diameter);
        state->base_diameter = diameter;
    }
    if (state->thumb_diameter != thumb_size) {
        lv_obj_set_size(state->thumb, thumb_size, thumb_size);
        state->thumb_diameter = thumb_size;
    }
    if (!state->geometry_initialized || state->base_position.x != base_position.x || state->base_position.y != base_position.y) {
        lv_obj_set_pos(state->base, base_position.x, base_position.y);
        state->base_position = base_position;
    }
    if (!state->geometry_initialized || state->thumb_position.x != thumb_position.x || state->thumb_position.y != thumb_position.y) {
        lv_obj_set_pos(state->thumb, thumb_position.x, thumb_position.y);
        state->thumb_position = thumb_position;
    }
    state->geometry_initialized = true;
    gamepad_joystick_color(state);
}

static void gamepad_joystick_cancel(GamepadJoystickState* state, const char* reason) {
    if (state->touch.owner) {
        LOGI("GamepadJoystick", "Release stick=%s owner=%lu generation=%lu reason=%s axes=0,0",
             state->config.stick ? "right" : "left", (unsigned long)state->touch.owner,
             (unsigned long)state->touch.generation, reason);
    }
    state->touch.cancel();
    state->input.cancel();
    state->axis_logged = false;
    state->input.thumb_x = state->input.center_x;
    state->input.thumb_y = state->input.center_y;
    if (state->touch.object) gamepad_joystick_render(state);
}

static void gamepad_joystick_point(void* context, GamepadTouchEvent requested, const lv_point_t& point) {
    auto* state = static_cast<GamepadJoystickState*>(context);
    const GamepadTouchEvent interaction = state->touch.point_event(requested);
    if (interaction == GamepadTouchEvent::Release || interaction == GamepadTouchEvent::Cancel) {
        gamepad_joystick_cancel(state, interaction == GamepadTouchEvent::Release ? "touch release" : "touch cancel");
        return;
    }
    if (interaction == GamepadTouchEvent::Press) {
        lv_area_t area;
        lv_obj_get_content_coords(state->button, &area);
        if (!state->input.press(point.x, point.y, area.x1, area.y1,
              area.x2 - area.x1 + 1, area.y2 - area.y1 + 1, state->config.floating)) {
              LOGW("GamepadJoystick", "Press rejected: content too small size=%ldx%ld",
                  (long)(area.x2 - area.x1 + 1), (long)(area.y2 - area.y1 + 1));
              return;
           }
        state->touch.owner = gamepad_hid_acquire_stick(state->config.stick, state->touch.generation);
           if (!state->touch.owner) {
              LOGW("GamepadJoystick", "Capture rejected stick=%s generation=%lu (busy, stale, or unavailable)",
                  state->config.stick ? "right" : "left", (unsigned long)state->touch.generation);
              gamepad_joystick_cancel(state, "capture rejected");
              return;
           }
           LOGI("GamepadJoystick", "Captured stick=%s owner=%lu generation=%lu point=%ld,%ld radius=%.1f",
               state->config.stick ? "right" : "left", (unsigned long)state->touch.owner,
               (unsigned long)state->touch.generation, (long)point.x, (long)point.y, double(state->input.radius));
    } else if (interaction != GamepadTouchEvent::Move) return;
    const auto position = state->input.move(point.x, point.y, state->config.dead_zone,
                                          state->config.invert_x, state->config.invert_y);
    if (!gamepad_hid_move(state->touch.owner, position.horizontal, position.vertical, state->touch.generation)) {
        LOGW("GamepadJoystick", "Axis update rejected owner=%lu generation=%lu",
             (unsigned long)state->touch.owner, (unsigned long)state->touch.generation);
        gamepad_joystick_cancel(state, "axis update rejected");
        return;
    }
    const uint32_t now = lv_tick_get();
    const bool first_deflection = !state->logged_horizontal && !state->logged_vertical &&
        (position.horizontal || position.vertical);
    if ((!state->axis_logged || position.horizontal != state->logged_horizontal || position.vertical != state->logged_vertical) &&
        (!state->axis_logged || first_deflection || now - state->last_axis_log >= 200)) {
        LOGI("GamepadJoystick", "Axes stick=%s owner=%lu x=%d y=%d",
             state->config.stick ? "right" : "left", (unsigned long)state->touch.owner,
             int(position.horizontal), int(position.vertical));
        state->last_axis_log = now;
        state->logged_horizontal = position.horizontal;
        state->logged_vertical = position.vertical;
        state->axis_logged = true;
    }
    gamepad_joystick_render(state);
}

static void gamepad_joystick_event(lv_event_t* event) {
    auto* state = static_cast<GamepadJoystickState*>(lv_event_get_user_data(event));
    lv_point_t point{};
    gamepad_joystick_point(state, state->touch.process(event, point), point);
}

static void gamepad_stick_create(lv_obj_t* button, const WidgetConfig* config,
                                   const ScreenButtonConfig*, const PadRect*, const UIScaleInfo*,
                                   lv_obj_t*, lv_obj_t* center_label, WidgetState* state) {
    auto* joystick = new (state->data) GamepadJoystickState{};
    joystick->button = button;
    joystick->touch.object = button;
    joystick->config = *reinterpret_cast<const GamepadJoystickConfig*>(config->data);
    joystick->base = lv_obj_create(button);
    joystick->thumb = lv_obj_create(button);
    for (lv_obj_t* object : {joystick->base, joystick->thumb}) {
        lv_obj_remove_style_all(object);
        lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(object, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    }
    lv_obj_set_style_border_width(joystick->base, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(joystick->thumb, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_update_layout(button);
    lv_area_t area;
    lv_obj_get_content_coords(button, &area);
    joystick->input.press(0, 0, area.x1, area.y1, area.x2 - area.x1 + 1, area.y2 - area.y1 + 1, false);
    gamepad_joystick_cancel(joystick, "initialize");
    if (center_label) lv_obj_add_flag(center_label, LV_OBJ_FLAG_HIDDEN);
    joystick->touch.attach(button, gamepad_joystick_event, joystick, gamepad_joystick_point);
        LOGI("GamepadJoystick", "Created stick=%s center=%s dead_zone=%.2f invert=%u,%u size=%ldx%ld ready=%u",
            joystick->config.stick ? "right" : "left", joystick->config.floating ? "floating" : "fixed",
            double(joystick->config.dead_zone), unsigned(joystick->config.invert_x), unsigned(joystick->config.invert_y),
            (long)(area.x2 - area.x1 + 1), (long)(area.y2 - area.y1 + 1), unsigned(gamepad_hid_is_ready()));
}
static void gamepad_stick_update(lv_obj_t*, const WidgetConfig*, WidgetState*, const char*) {}
static void gamepad_stick_tick(lv_obj_t*, const WidgetConfig*, WidgetState* state) {
    auto* joystick = reinterpret_cast<GamepadJoystickState*>(state->data);
    if (!joystick->touch.object) return;
    gamepad_joystick_color(joystick);
    if (joystick->touch.owner && (!gamepad_hid_is_ready() || joystick->touch.generation != gamepad_hid_generation()))
        gamepad_joystick_cancel(joystick, "transport reset");
}
static void gamepad_stick_hide(WidgetState* state) {
    reinterpret_cast<GamepadJoystickState*>(state->data)->touch.hide();
}
static void gamepad_stick_show(WidgetState* state) {
    reinterpret_cast<GamepadJoystickState*>(state->data)->touch.show();
}
static void gamepad_stick_destroy(WidgetState* state) {
    auto* joystick = reinterpret_cast<GamepadJoystickState*>(state->data);
    joystick->touch.detach(gamepad_joystick_event);
    joystick->~GamepadJoystickState();
}
#if HAS_MCP
static void gamepad_stick_describe(JsonObject& out) {
    JsonArray fields = out.createNestedArray("config_fields");
    const char* names[] = {"widget_gamepad_stick", "widget_gamepad_center", "widget_gamepad_dead_zone", "widget_gamepad_invert_x", "widget_gamepad_invert_y"};
    const char* descriptions[] = {"left (default) or right", "fixed (default) or floating", "Radial dead zone 0-0.9 (default 0.1)", "Invert horizontal axis (default false)", "Invert vertical axis (default false)"};
    for (size_t index = 0; index < 5; ++index) {
        JsonObject field = fields.createNestedObject();
        field["name"] = names[index];
        field["type"] = index < 2 ? "string" : index == 2 ? "number" : "boolean";
        field["desc"] = descriptions[index];
    }
    out["note"] = "Absolute USB gamepad stick, with independent simultaneous contacts on multitouch drivers; release/cancel returns to neutral. One contact per widget; extras are ignored until lift. Ring fills the shorter padded content dimension; dot is one-third of its diameter (minimum 6 pixels) and stays inside the ring. Both use button foreground color. Floating center can shift only along the longer dimension. Areas smaller than 16 pixels are inactive. First interaction owns each stick until release. Positive X/Y are right/down. Center label is suppressed; top/bottom labels remain. Consumes normal actions and pad swipes. USB keyboard mode required.";
}
#endif
static const WidgetPreview gamepad_stick_preview = {"Gamepad Joystick", "joystick"};
REGISTER_WIDGET_SCHEMA_VALIDATED_LIFECYCLE(gamepad_stick, nullptr, false, gamepad_stick_validate);
#endif