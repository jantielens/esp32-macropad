#include "widget.h"

#if HAS_DISPLAY && HAS_TOUCH && HAS_USB_HID
#include "gamepad_surface_touch.h"
#include <new>

struct GamepadButtonState {
    GamepadSurfaceTouch touch;
    GamepadControl control{GamepadControlKind::Button, 0};
    lv_obj_t* button = nullptr;
    bool configured = false;
};
static_assert(sizeof(GamepadButtonState) <= WIDGET_STATE_MAX_BYTES, "Gamepad button state too large");

static const char* gamepad_button_validate(JsonObjectConst button) {
    JsonArrayConst actions = button["actions"];
    if (!button["actions"].is<JsonArrayConst>() || actions.size() != 1)
        return "gamepad_button requires exactly one Gamepad down action in actions";
    JsonObjectConst action = actions[0];
    if (strcmp(action["type"] | "", ACTION_TYPE_GAMEPAD) != 0 || strcmp(action["operation"] | "tap", "down") != 0)
        return "gamepad_button requires a Gamepad down action; release is automatic";
    if (button["lp_actions"].size() || button.containsKey("action") || button.containsKey("lp_action"))
        return "gamepad_button does not support long-press or legacy action fields";
    return nullptr;
}

static void gamepad_button_parse(const JsonObject&, uint8_t*) {}

static void gamepad_button_cancel(GamepadButtonState* state, const char* reason,
                                 lv_event_code_t feedback = LV_EVENT_PRESS_LOST) {
    const bool was_held = state->touch.owner != 0;
    if (state->touch.owner) {
        LOGI("GamepadButton", "Release owner=%lu generation=%lu reason=%s",
             (unsigned long)state->touch.owner, (unsigned long)state->touch.generation, reason);
    }
    state->touch.cancel();
    if (state->button) lv_obj_remove_state(state->button, LV_STATE_PRESSED);
    if (was_held && state->button) lv_obj_send_event(state->button, LV_EVENT_VALUE_CHANGED, &feedback);
}

static void gamepad_button_event(lv_event_t* event) {
    auto* state = static_cast<GamepadButtonState*>(lv_event_get_user_data(event));
    lv_point_t point;
    const GamepadTouchEvent interaction = state->touch.process(event, point);
    if (interaction == GamepadTouchEvent::Press) {
        if (!state->configured) {
            LOGW("GamepadButton", "Press rejected: invalid held-control configuration");
            lv_obj_remove_state(state->button, LV_STATE_PRESSED);
            return;
        }
        state->touch.owner = gamepad_hid_acquire(state->control, state->touch.generation);
        if (!state->touch.owner) {
            LOGW("GamepadButton", "Capture rejected control=%u index=%u generation=%lu",
                 unsigned(state->control.kind), unsigned(state->control.index), (unsigned long)state->touch.generation);
            lv_obj_remove_state(state->button, LV_STATE_PRESSED);
        } else {
            lv_obj_add_state(state->button, LV_STATE_PRESSED);
            lv_event_code_t feedback = LV_EVENT_PRESSED;
            lv_obj_send_event(state->button, LV_EVENT_VALUE_CHANGED, &feedback);
            LOGI("GamepadButton", "Captured control=%u index=%u owner=%lu generation=%lu",
                 unsigned(state->control.kind), unsigned(state->control.index),
                 (unsigned long)state->touch.owner, (unsigned long)state->touch.generation);
        }
    } else if (interaction == GamepadTouchEvent::Release || interaction == GamepadTouchEvent::Cancel) {
        gamepad_button_cancel(state, interaction == GamepadTouchEvent::Release ? "touch release" : "touch cancel",
            interaction == GamepadTouchEvent::Release ? LV_EVENT_RELEASED : LV_EVENT_PRESS_LOST);
    }
}

static void gamepad_button_create(lv_obj_t* button, const WidgetConfig*,
                                 const ScreenButtonConfig* config, const PadRect*,
                                 const UIScaleInfo*, lv_obj_t*, lv_obj_t*, WidgetState* state) {
    auto* held = new (state->data) GamepadButtonState{};
    held->button = button;
    if (config->action_count == 1 && strcmp(config->actions[0].type, ACTION_TYPE_GAMEPAD) == 0 &&
        config->actions[0].payload.gamepad.operation == 1) {
        const auto& payload = config->actions[0].payload.gamepad;
        held->control = {static_cast<GamepadControlKind>(payload.control), payload.index};
        held->configured = held->control.mask() != 0;
    }
    GamepadSurfaceTouch::attach(button, gamepad_button_event, held);
        LOGI("GamepadButton", "Created configured=%u control=%u index=%u ready=%u",
            unsigned(held->configured), unsigned(held->control.kind), unsigned(held->control.index), unsigned(gamepad_hid_is_ready()));
}
static void gamepad_button_update(lv_obj_t*, const WidgetConfig*, WidgetState*, const char*) {}
static void gamepad_button_tick(lv_obj_t*, const WidgetConfig*, WidgetState* state) {
    auto* held = reinterpret_cast<GamepadButtonState*>(state->data);
    if (held->touch.owner && (!gamepad_hid_is_ready() || held->touch.generation != gamepad_hid_generation()))
        gamepad_button_cancel(held, "transport reset");
}
    static void gamepad_button_hide(WidgetState* state) { gamepad_button_cancel(reinterpret_cast<GamepadButtonState*>(state->data), "hide/show/destroy"); }
static void gamepad_button_show(WidgetState* state) { gamepad_button_hide(state); }
static void gamepad_button_destroy(WidgetState* state) {
    gamepad_button_hide(state);
    lv_obj_remove_event_cb(reinterpret_cast<GamepadButtonState*>(state->data)->button, gamepad_button_event);
    reinterpret_cast<GamepadButtonState*>(state->data)->~GamepadButtonState();
}
#if HAS_MCP
static void gamepad_button_describe(JsonObject& out) {
    out["note"] = "Single-touch held Gamepad control. Configure actions as exactly one gamepad action with operation down. Touch release, cancellation, hiding, pad exit, disconnect, and OTA release automatically. No long-press actions, ordinary tap dispatch, or pad swipes. USB keyboard mode required.";
    JsonObject field = out.createNestedArray("config_fields").createNestedObject();
    field["name"] = "actions";
    field["type"] = "array";
    field["desc"] = "Exactly one {type:gamepad, control:button|hat|trigger, operation:down, ...target fields}";
}
#endif
REGISTER_WIDGET_SCHEMA_VALIDATED_LIFECYCLE(gamepad_button, nullptr, false, gamepad_button_validate);
#endif