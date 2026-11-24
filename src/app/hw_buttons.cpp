#include "hw_buttons.h"

#if HAS_BUTTON

#include "action_dispatch.h"
#include "action_list.h"
#include "action_registry.h"
#include "action_storage.h"
#include "hw_button_config.h"
#include "log_manager.h"
#if HAS_AUTO_SHUTDOWN
#include "auto_shutdown.h"
#endif
#if HAS_DISPLAY
#include "device_dialog.h"
#endif

#if HAS_AUDIO
#include "audio.h"
#include "config_manager.h"
extern DeviceConfig device_config;
#endif

#include <Arduino.h>

#define TAG "HwBtn"

// Debounce window (ms), applied to both press and release. Bounce during the
// initial press is rejected in Debounce; a release is only committed after the
// line stays inactive for this window (TapRelease / HoldRelease).
#define HW_BUTTON_DEBOUNCE_MS 20

enum class BtnState : uint8_t { Idle, Debounce, Pressed, TapRelease, Held, HoldRelease };

struct BtnRuntime {
    BtnState state;
    uint32_t t_change;  // millis() at last state transition
    uint32_t dialog_generation;
};

static BtnRuntime s_rt[NUM_HW_BUTTONS];

// ISR-set flag: an edge occurred on at least one button. Protected by a
// portMUX spinlock for safe read-modify-write between ISR and loop().
static volatile bool s_edge_pending = false;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static void IRAM_ATTR hw_button_isr() {
    portENTER_CRITICAL_ISR(&s_mux);
    s_edge_pending = true;
    portEXIT_CRITICAL_ISR(&s_mux);
}

static inline bool button_pressed(uint8_t i) {
    int level = digitalRead(HW_BUTTON_DEFS[i].pin);
    return HW_BUTTON_DEFS[i].active_low ? (level == LOW) : (level == HIGH);
}

#if HAS_AUDIO
// Play the gesture's feedback cue, unless the action list itself produces audio.
static void maybe_beep(const ButtonAction* actions, uint8_t count, bool is_hold) {
    if (action_list_has_audio(actions, count)) {
        return;
    }
    const char* pattern = is_hold ? device_config.lp_beep : device_config.tap_beep;
    if (pattern[0] && strcmp(pattern, "none") != 0) {
        audio_feedback(pattern);
    }
}
#endif

static void dispatch_event(uint8_t index, bool is_hold) {
#if HAS_DISPLAY
    if (s_rt[index].dialog_generation != device_dialog_input_generation()) return;
#endif
    auto* actions = static_cast<ButtonAction*>(action_storage_allocate(MAX_BUTTON_ACTIONS * sizeof(ButtonAction)));
    if (!actions) {
        LOGE(TAG, "Cannot allocate hardware button action snapshot");
        return;
    }
    ActionStorage storage = {};
    uint8_t count = 0;
    if (!hw_button_config_snapshot(index, is_hold, actions, &count, storage)) {
        LOGE(TAG, "Cannot copy hardware button %u actions", index);
        action_storage_clear(storage);
        action_storage_free(actions);
        return;
    }
#if HAS_DISPLAY
    if (device_dialog_active()) {
        for (uint8_t i = 0; i < count; ++i) {
            const ActionOperationDef* operation = action_operation_for_action(actions[i]);
            if (operation && !strcmp(operation->id, "button.navigate")) {
                action_dispatch(actions[i], "Dialog navigation");
                break;
            }
        }
        action_storage_clear(storage);
        action_storage_free(actions);
        return;
    }
#endif
    LOGI(TAG, "HW btn %u %s: dispatching %u action(s)", index, is_hold ? "hold" : "tap", count);
    if (count == 0) {
        action_storage_clear(storage);
        action_storage_free(actions);
        return;
    }
#if HAS_AUDIO
    maybe_beep(actions, count, is_hold);
#endif
    char label[16];
    snprintf(label, sizeof(label), "HWBtn%u%s", index, is_hold ? "H" : "T");
    action_list_dispatch(actions, count, label, ACTION_CONTINUATION_OWNER_LOOP, true);
    action_storage_clear(storage);
    action_storage_free(actions);
}

void hw_buttons_init() {
    for (uint8_t i = 0; i < NUM_HW_BUTTONS; i++) {
        const HwButtonDef& def = HW_BUTTON_DEFS[i];
        pinMode(def.pin, def.active_low ? INPUT_PULLUP : INPUT_PULLDOWN);
        s_rt[i].state = BtnState::Idle;
        s_rt[i].t_change = 0;
        attachInterrupt(digitalPinToInterrupt(def.pin), hw_button_isr, CHANGE);
        LOGI(TAG, "Button %u: GPIO%u active-%s label='%s'", i, def.pin,
             def.active_low ? "low" : "high", def.label);
    }
    LOGI(TAG, "Initialized %u hardware button(s)", (unsigned)NUM_HW_BUTTONS);
}

void hw_buttons_loop() {
    // Fast path: nothing to do unless an edge fired or a button is mid-press.
    bool edge;
    portENTER_CRITICAL(&s_mux);
    edge = s_edge_pending;
    s_edge_pending = false;
    portEXIT_CRITICAL(&s_mux);

    if (!edge) {
        bool any_active = false;
        for (uint8_t i = 0; i < NUM_HW_BUTTONS; i++) {
            if (s_rt[i].state != BtnState::Idle) { any_active = true; break; }
        }
        if (!any_active) return;
    }

    const uint32_t now = millis();
    for (uint8_t i = 0; i < NUM_HW_BUTTONS; i++) {
        const bool active = button_pressed(i);
        BtnRuntime& rt = s_rt[i];
#if HAS_AUTO_SHUTDOWN
        if (active && rt.state != BtnState::Idle && rt.state != BtnState::Debounce)
            auto_shutdown_notify_activity();
#endif
        switch (rt.state) {
            case BtnState::Idle:
                if (active) {
                    rt.state = BtnState::Debounce;
                    rt.t_change = now;
#if HAS_DISPLAY
                    rt.dialog_generation = device_dialog_input_generation();
#endif
                }
                break;
            case BtnState::Debounce:
                if (!active) {
                    rt.state = BtnState::Idle;  // bounce during initial press
                } else if (now - rt.t_change >= HW_BUTTON_DEBOUNCE_MS) {
                    rt.state = BtnState::Pressed;
                    rt.t_change = now;
                }
                break;
            case BtnState::Pressed:
                if (!active) {
                    rt.state = BtnState::TapRelease;  // candidate tap; debounce release
                    rt.t_change = now;
                } else if (now - rt.t_change >= HW_BUTTON_HOLD_MS) {
                    dispatch_event(i, true);   // held past threshold → hold
                    rt.state = BtnState::Held;
                }
                break;
            case BtnState::TapRelease:
                if (active) {
                    rt.state = BtnState::Pressed;  // bounce; resume press
                    rt.t_change = now;
                } else if (now - rt.t_change >= HW_BUTTON_DEBOUNCE_MS) {
                    dispatch_event(i, false);  // release confirmed → tap
                    rt.state = BtnState::Idle;
                }
                break;
            case BtnState::Held:
                if (!active) {
                    rt.state = BtnState::HoldRelease;  // debounce release before Idle
                    rt.t_change = now;
                }
                break;
            case BtnState::HoldRelease:
                if (active) {
                    rt.state = BtnState::Held;  // bounce; still held (hold already fired)
                } else if (now - rt.t_change >= HW_BUTTON_DEBOUNCE_MS) {
                    rt.state = BtnState::Idle;  // release confirmed
                }
                break;
        }
    }
}

#endif  // HAS_BUTTON
