#include "widget.h"

#if HAS_DISPLAY && HAS_TOUCH && (HAS_USB_HID || HAS_BLE_HID)
#include "gamepad_surface_touch.h"
#include "../action_registry.h"
#include "../action_storage.h"
#include <new>

struct GamepadButtonState {
    GamepadSurfaceTouch touch;
    GamepadControl control{GamepadControlKind::Button, 0};
    lv_obj_t* button = nullptr;
    bool configured = false;
};
static_assert(sizeof(GamepadButtonState) <= WIDGET_STATE_MAX_BYTES, "Gamepad button state too large");

static int gamepad_button_kind(const char* operation) {
    if (!strcmp(operation, "gamepad.button.down")) return 0;
    if (!strcmp(operation, "gamepad.hat.down")) return 1;
    if (!strcmp(operation, "gamepad.trigger.down")) return 2;
    return -1;
}

static bool gamepad_button_control(int kind, int32_t number, const char* text, GamepadControl& control) {
    if (kind == 0) {
        if (number < 1 || number > gamepad_protocol::button_count) return false;
        control = {GamepadControlKind::Button, uint8_t(number - 1)};
        return true;
    }
    if (!text) return false;
    const char* const directions[] = {"up", "down", "left", "right"};
    const char* const triggers[] = {"left", "right"};
    const char* const* choices = kind == 1 ? directions : kind == 2 ? triggers : nullptr;
    const unsigned count = kind == 1 ? gamepad_protocol::hat_control_count :
        kind == 2 ? gamepad_protocol::trigger_count : 0;
    for (unsigned index = 0; index < count; ++index) {
        if (!strcmp(text, choices[index])) {
            control = {kind == 1 ? GamepadControlKind::Hat : GamepadControlKind::Trigger, uint8_t(index)};
            return true;
        }
    }
    return false;
}

static const char* gamepad_button_validate(JsonObjectConst button) {
    JsonArrayConst actions = button["actions"];
    if (!button["actions"].is<JsonArrayConst>() || actions.size() != 1)
        return "gamepad_button requires exactly one Gamepad down action in actions";
    JsonObjectConst action = actions[0];
    if (action["op"].is<const char*>()) {
        const int kind = gamepad_button_kind(action["op"].as<const char*>());
        if (kind < 0 || !action["args"].is<JsonObjectConst>())
            return "gamepad_button requires a Gamepad down operation; release is automatic";
        const char* field = kind == 0 ? "button" : kind == 1 ? "direction" : "trigger";
        JsonVariantConst value = action["args"][field];
        GamepadControl control{};
        if (value.is<JsonObjectConst>())
            return "gamepad_button requires a literal held control, not a dynamic argument";
        if (action.size() != 2 || action["args"].size() != 1 ||
            (kind == 0 ? !value.is<int32_t>() : !value.is<const char*>()) ||
            !gamepad_button_control(kind, value.as<int32_t>(), value.as<const char*>(), control))
            return "gamepad_button requires a valid literal held control";
    } else {
        return "gamepad_button requires a Gamepad down action; release is automatic";
    }
    if (button["lp_actions"].size() || button.containsKey("action") || button.containsKey("lp_action"))
        return "gamepad_button does not support long-press or legacy action fields";
    return nullptr;
}

static void gamepad_button_parse(const JsonObject&, uint8_t*) {}

static void gamepad_button_cancel(GamepadButtonState* state, const char* reason,
                                 lv_event_code_t feedback = LV_EVENT_PRESS_LOST) {
    const bool was_held = state->touch.owner != 0;
    if (state->touch.owner) {
        LOGT("GamepadButton", "Release owner=%lu generation=%lu reason=%s",
             (unsigned long)state->touch.owner, (unsigned long)state->touch.generation, reason);
    }
    state->touch.cancel();
    if (state->touch.object) lv_obj_remove_state(state->button, LV_STATE_PRESSED);
    if (was_held && state->touch.object) lv_obj_send_event(state->button, LV_EVENT_VALUE_CHANGED, &feedback);
}

static void gamepad_button_point(void* context, GamepadTouchEvent requested, const lv_point_t&) {
    auto* state = static_cast<GamepadButtonState*>(context);
    const GamepadTouchEvent interaction = state->touch.point_event(requested);
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
            LOGT("GamepadButton", "Captured control=%u index=%u owner=%lu generation=%lu",
                 unsigned(state->control.kind), unsigned(state->control.index),
                 (unsigned long)state->touch.owner, (unsigned long)state->touch.generation);
              lv_obj_send_event(state->button, LV_EVENT_VALUE_CHANGED, &feedback);
        }
    } else if (interaction == GamepadTouchEvent::Release || interaction == GamepadTouchEvent::Cancel) {
        gamepad_button_cancel(state, interaction == GamepadTouchEvent::Release ? "touch release" : "touch cancel",
            interaction == GamepadTouchEvent::Release ? LV_EVENT_RELEASED : LV_EVENT_PRESS_LOST);
    }
}

static void gamepad_button_event(lv_event_t* event) {
    auto* state = static_cast<GamepadButtonState*>(lv_event_get_user_data(event));
    lv_point_t point{};
    gamepad_button_point(state, state->touch.process(event, point), point);
}

static void gamepad_button_create(lv_obj_t* button, const WidgetConfig*,
                                 const ScreenButtonConfig* config, const PadRect*,
                                 const UIScaleInfo*, WidgetState* state) {
    auto* held = new (state->data) GamepadButtonState{};
    held->button = button;
    if (config->action_count == 1) {
        const ButtonAction& action = config->actions[0];
        if (action.type[0] == '@' && action.payload.operation.descriptor &&
            action.payload.operation.arguments) {
            const auto* operation = action.payload.operation.descriptor;
            const auto* block = action.payload.operation.arguments;
            const int kind = gamepad_button_kind(operation->id);
            if (kind >= 0 && operation->argument_count == 1 && block->count == 1) {
                const ActionStoredArgument& argument = action_storage_arguments(block)[0];
                if (!argument.dynamic) {
                    held->configured = gamepad_button_control(
                        kind, argument.integer, kind == 0 ? nullptr : action_storage_text(block, argument),
                        held->control);
                }
            }
        }
    }
    held->touch.attach(button, gamepad_button_event, held, gamepad_button_point);
        LOGT("GamepadButton", "Created configured=%u control=%u index=%u ready=%u",
            unsigned(held->configured), unsigned(held->control.kind), unsigned(held->control.index), unsigned(gamepad_hid_is_ready()));
}
static void gamepad_button_update(lv_obj_t*, const WidgetConfig*, WidgetState*, const char*) {}
static void gamepad_button_tick(lv_obj_t*, const WidgetConfig*, WidgetState* state) {
    auto* held = reinterpret_cast<GamepadButtonState*>(state->data);
    if (held->touch.owner && (!gamepad_hid_is_ready() || held->touch.generation != gamepad_hid_generation()))
        gamepad_button_cancel(held, "transport reset");
}
static void gamepad_button_hide(WidgetState* state) {
    reinterpret_cast<GamepadButtonState*>(state->data)->touch.hide();
}
static void gamepad_button_show(WidgetState* state) {
    reinterpret_cast<GamepadButtonState*>(state->data)->touch.show();
}
static void gamepad_button_cancel_input(WidgetState* state) {
    gamepad_button_cancel(reinterpret_cast<GamepadButtonState*>(state->data), "input cancelled");
}
static void gamepad_button_destroy(WidgetState* state) {
    auto* held = reinterpret_cast<GamepadButtonState*>(state->data);
    held->touch.detach(gamepad_button_event);
    held->~GamepadButtonState();
}
#if HAS_MCP
static void gamepad_button_describe(JsonObject& out) {
    out["note"] = "Held Gamepad control, with independent simultaneous contacts on multitouch drivers. One contact per widget; extras are ignored until lift. Configure exactly one gamepad.button.down, gamepad.hat.down, or gamepad.trigger.down operation with a literal target. Touch release, cancellation, hiding, pad exit, disconnect, and OTA release automatically. No long-press actions, ordinary tap dispatch, or pad swipes. USB or BLE HID transport required.";
    JsonObject field = out.createNestedArray("config_fields").createNestedObject();
    field["name"] = "actions";
    field["type"] = "array";
    field["desc"] = "Exactly one gamepad.button.down, gamepad.hat.down, or gamepad.trigger.down operation with a literal target";
}
#endif
static const WidgetPreview gamepad_button_preview = {"Gamepad Button", "gamepad"};
REGISTER_WIDGET_SCHEMA_INPUT_LIFECYCLE(gamepad_button, nullptr, false, gamepad_button_validate, gamepad_button_cancel_input);
#endif