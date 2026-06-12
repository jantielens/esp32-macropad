#include "hw_buttons.h"

#if HAS_BUTTON

#include "action_dispatch.h"
#include "action_list.h"
#include "hw_button_config.h"
#include "log_manager.h"

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
// Play the device tap_beep cue, unless the action list itself produces audio.
static void maybe_beep(const ButtonAction* actions, uint8_t count) {
    if (count > 0 &&
        (strcmp(actions[0].type, ACTION_TYPE_BEEP) == 0 ||
         strcmp(actions[0].type, ACTION_TYPE_SOUND) == 0)) {
        return;
    }
    const char* pattern = device_config.tap_beep;
    if (pattern[0] && strcmp(pattern, "none") != 0) {
        audio_beep(pattern, 0);
    }
}
#endif

static void dispatch_event(uint8_t index, bool is_hold) {
    const HwButtonConfig* cfg = hw_button_config_get(index);
    if (!cfg) return;
    const ButtonAction* actions = is_hold ? cfg->hold_actions : cfg->tap_actions;
    uint8_t count = is_hold ? cfg->hold_count : cfg->tap_count;
    LOGI(TAG, "HW btn %u %s: dispatching %u action(s)", index, is_hold ? "hold" : "tap", count);
    if (count == 0) return;
#if HAS_AUDIO
    // tap_beep is the tap feedback cue only; hold events do not beep.
    if (!is_hold) maybe_beep(actions, count);
#endif
    char label[16];
    snprintf(label, sizeof(label), "HWBtn%u%s", index, is_hold ? "H" : "T");
    action_list_dispatch(actions, count, label);
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
        switch (rt.state) {
            case BtnState::Idle:
                if (active) { rt.state = BtnState::Debounce; rt.t_change = now; }
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
