#include "action_dispatch.h"

#if HAS_DISPLAY

#include "display_manager.h"
#include "log_manager.h"
#include "message_bubble.h"

#if HAS_MQTT
#include "binding_template.h"
#include "mqtt_manager.h"
#endif
#if HAS_BLE_HID
#include "ble_hid.h"
#endif
#if HAS_AUDIO
#include "audio.h"
#endif

#include "timer_engine.h"
#include "wifi_manager.h"
#include "screen_saver_manager.h"

#if IS_SHUTTER_TESTER
#include "device_classes/shutter_tester/shutter_measure.h"
#include "device_classes/shutter_tester/shutter_session.h"
#include "device_classes/shutter_tester/shutter_capture.h"
#endif

#include <math.h>

#define TAG "Action"

// Compute a clamped percentage value from a string, optionally as a delta from current.
static uint8_t compute_clamped_percent(const char* value_str, uint8_t current, bool is_adjust, int min_val) {
    int v = is_adjust ? (int)current + lroundf(atof(value_str)) : lroundf(atof(value_str));
    if (v > 100) v = 100;
    if (v < min_val) v = min_val;
    return (uint8_t)v;
}

#if HAS_MQTT
// Quick scan: return true if any data/content field might contain a binding token.
// Checks only for '[' — avoids the ~1.2 KB ButtonAction copy for the common case.
// IMPORTANT: the field list here must exactly match the fields in resolve_action_bindings below.
static bool action_has_any_binding(const ButtonAction& act) {
    const char* fields[] = {
        act.screen_id,
        act.mqtt_topic, act.mqtt_payload, act.key_sequence, act.beep_pattern,
        act.volume_value, act.brightness_value, act.timer_value,
        act.notify_text, act.notify_duration_ms,
        act.notify_text_color, act.notify_bg_color, act.notify_border_color
    };
    for (auto f : fields) {
        if (f[0] && memchr(f, '[', strlen(f))) return true;
    }
    return false;
}

// Resolve binding templates in all resolvable fields of a ButtonAction.
// Structural fields (type, commands, modes, etc.) are excluded.
// IMPORTANT: the field list here must exactly match the fields in action_has_any_binding above.
static void resolve_action_bindings(ButtonAction& act) {
    auto try_resolve = [](char* field, size_t len) {
        if (field[0] && binding_template_has_bindings(field)) {
            char tmp[BINDING_TEMPLATE_MAX_LEN];
            binding_template_resolve(field, tmp, sizeof(tmp));
            strlcpy(field, tmp, len);
        }
    };

    try_resolve(act.screen_id,           sizeof(act.screen_id));
    try_resolve(act.mqtt_topic,          sizeof(act.mqtt_topic));
    try_resolve(act.mqtt_payload,        sizeof(act.mqtt_payload));
    try_resolve(act.key_sequence,        sizeof(act.key_sequence));
    try_resolve(act.beep_pattern,        sizeof(act.beep_pattern));
    try_resolve(act.volume_value,        sizeof(act.volume_value));
    try_resolve(act.brightness_value,    sizeof(act.brightness_value));
    try_resolve(act.timer_value,         sizeof(act.timer_value));
    try_resolve(act.notify_text,         sizeof(act.notify_text));
    try_resolve(act.notify_duration_ms,  sizeof(act.notify_duration_ms));
    try_resolve(act.notify_text_color,   sizeof(act.notify_text_color));
    try_resolve(act.notify_bg_color,     sizeof(act.notify_bg_color));
    try_resolve(act.notify_border_color, sizeof(act.notify_border_color));
}
#endif // HAS_MQTT

static void action_dispatch_resolved(const ButtonAction& act, const char* label);

void action_dispatch(const ButtonAction& act_in, const char* label) {
    if (!act_in.type[0]) return;

    // Resolve binding templates in value fields before dispatch.
    // Must only be called from the LVGL task — binding_template_resolve
    // accesses MQTT subscription state and may call LVGL APIs.
#if HAS_MQTT
    if (action_has_any_binding(act_in)) {
        ButtonAction act = act_in;
        resolve_action_bindings(act);
        action_dispatch_resolved(act, label);
    } else {
        action_dispatch_resolved(act_in, label);
    }
#else
    action_dispatch_resolved(act_in, label);
#endif
}

static void action_dispatch_resolved(const ButtonAction& act, const char* label) {

    if (strcmp(act.type, ACTION_TYPE_SCREEN) == 0) {
        if (act.screen_id[0]) {
            bool ok = false;
            display_manager_show_screen(act.screen_id, &ok);
            if (!ok) {
                LOGW(TAG, "%s nav failed: '%s'", label, act.screen_id);
            }
        }
    } else if (strcmp(act.type, ACTION_TYPE_BACK) == 0) {
        if (!display_manager_go_back()) {
            LOGW(TAG, "%s back: no previous screen", label);
        }
    } else if (strcmp(act.type, ACTION_TYPE_MQTT) == 0) {
#if HAS_MQTT
        if (act.mqtt_topic[0]) {
            bool ok = mqtt_manager.publish(act.mqtt_topic, act.mqtt_payload, false);
            LOGI(TAG, "%s mqtt: topic='%s' payload='%s' %s", label, act.mqtt_topic, act.mqtt_payload, ok ? "ok" : "FAIL");
        } else {
            LOGW(TAG, "%s mqtt: empty topic", label);
        }
#else
        LOGW(TAG, "%s mqtt: not compiled", label);
#endif
    } else if (strcmp(act.type, ACTION_TYPE_KEY) == 0) {
#if HAS_BLE_HID
        if (!ble_hid_is_initialized()) {
            LOGW(TAG, "%s key: BLE disabled", label);
        } else if (act.key_sequence[0]) {
            LOGI(TAG, "%s key: '%s'", label, act.key_sequence);
            ble_hid_request_sequence(act.key_sequence);
        } else {
            LOGW(TAG, "%s key: empty sequence", label);
        }
#else
        LOGW(TAG, "%s key: not compiled", label);
#endif
    } else if (strcmp(act.type, ACTION_TYPE_BLE_PAIR) == 0) {
#if HAS_BLE_HID
        if (!ble_hid_is_initialized()) {
            LOGW(TAG, "%s ble_pair: BLE disabled", label);
        } else {
            LOGI(TAG, "%s ble_pair: starting re-pairing", label);
            ble_hid_request_pairing();
        }
#else
        LOGW(TAG, "%s ble_pair: not compiled", label);
#endif
    } else if (strcmp(act.type, ACTION_TYPE_BEEP) == 0) {
#if HAS_AUDIO
        LOGI(TAG, "%s beep: pattern='%s' vol=%s", label, act.beep_pattern,
             act.beep_volume > 0 ? String(act.beep_volume).c_str() : "device");
        audio_beep(act.beep_pattern, act.beep_volume);
#else
        LOGW(TAG, "%s beep: not compiled", label);
#endif
    } else if (strcmp(act.type, ACTION_TYPE_SOUND) == 0) {
#if HAS_SOUND_PLAYER
        if (act.sound_file[0]) {
            LOGI(TAG, "%s sound: file='%s' vol=%s", label, act.sound_file,
                 act.sound_volume > 0 ? String(act.sound_volume).c_str() : "device");
            audio_play_sound(act.sound_file, act.sound_volume);
        } else {
            LOGW(TAG, "%s sound: empty filename", label);
        }
#else
        LOGW(TAG, "%s sound: not compiled", label);
#endif
    } else if (strcmp(act.type, ACTION_TYPE_VOLUME) == 0) {
#if HAS_AUDIO
        bool adj = strcmp(act.volume_mode, "adjust") == 0;
        uint8_t nv = compute_clamped_percent(act.volume_value, audio_get_volume(), adj, 0);
        audio_set_volume(nv);
        LOGI(TAG, "%s volume %s %s -> %u%%", label, adj ? "adjust" : "set", act.volume_value, nv);
#else
        LOGW(TAG, "%s volume: not compiled", label);
#endif
    } else if (strcmp(act.type, ACTION_TYPE_BRIGHTNESS) == 0) {
        bool adj = strcmp(act.brightness_mode, "adjust") == 0;
        uint8_t nv = compute_clamped_percent(act.brightness_value, display_manager_get_backlight_brightness(), adj, MIN_USER_BRIGHTNESS);
        screen_saver_manager_set_brightness(nv);
        LOGI(TAG, "%s brightness %s %s -> %u%%", label, adj ? "adjust" : "set", act.brightness_value, nv);
    } else if (strcmp(act.type, ACTION_TYPE_TIMER) == 0) {
        uint8_t tid = act.timer_id;
        const char* cmd = act.timer_command;
        if (tid >= 1 && tid <= TIMER_COUNT && cmd[0]) {
            if (strcmp(cmd, "start") == 0) {
                timer_start(tid);
            } else if (strcmp(cmd, "stop") == 0) {
                timer_stop(tid);
            } else if (strcmp(cmd, "toggle") == 0) {
                timer_toggle(tid);
            } else if (strcmp(cmd, "pause") == 0) {
                timer_pause(tid);
            } else if (strcmp(cmd, "resume") == 0) {
                timer_resume(tid);
            } else if (strcmp(cmd, "reset") == 0) {
                timer_reset(tid);
            } else if (strcmp(cmd, "lap") == 0) {
                timer_lap(tid);
            } else if (strcmp(cmd, "adjust") == 0) {
                int32_t delta = lroundf(atof(act.timer_value));
                timer_adjust(tid, delta);
            } else if (strcmp(cmd, "set") == 0) {
                uint32_t secs = (uint32_t)lroundf(atof(act.timer_value));
                timer_set_countdown(tid, secs);
            } else {
                LOGW(TAG, "%s timer: unknown cmd '%s'", label, cmd);
            }
            LOGI(TAG, "%s timer: %u:%s", label, tid, cmd);
        } else {
            LOGW(TAG, "%s timer: bad id=%u cmd='%s'", label, tid, cmd);
        }
    } else if (strcmp(act.type, ACTION_TYPE_NOTIFY) == 0) {
        MessageBubbleParams params = {};

        strlcpy(params.text, act.notify_text, sizeof(params.text));

        if (!params.text[0]) {
            message_bubble_dismiss();
            LOGI(TAG, "%s notify: dismiss", label);
        } else {
            // Duration — apply default for empty (already resolved by generic pass)
            const char* dur = act.notify_duration_ms[0] ? act.notify_duration_ms : "3000";
            params.duration_ms = (uint16_t)atoi(dur);

            // Colors — apply defaults for empty (already resolved by generic pass)
            const char* tc = act.notify_text_color[0] ? act.notify_text_color : "#ffffff";
            if (!parse_hex_color(tc, &params.text_color)) params.text_color = 0xFFFFFF;

            const char* bg = act.notify_bg_color[0] ? act.notify_bg_color : "#333333";
            if (!parse_hex_color(bg, &params.bg_color)) params.bg_color = 0x333333;

            if (act.notify_border_color[0]) {
                params.has_border = parse_hex_color(act.notify_border_color, &params.border_color);
            }

            params.opacity = act.notify_opacity;
            params.font_size = act.notify_font_size;
            params.location = notify_location_from_str(act.notify_location);

            message_bubble_show(&params);
            LOGI(TAG, "%s notify: '%s' dur=%u loc=%s", label, params.text,
                 params.duration_ms, act.notify_location[0] ? act.notify_location : "bottom");
        }
#if IS_SHUTTER_TESTER
    } else if (strcmp(act.type, ACTION_TYPE_SHUTTER) == 0) {
        const char* cmd = act.shutter_command;
        if (strcmp(cmd, "set") == 0) {
            if (!shutter_measure_set_target(act.shutter_value)) {
                LOGW(TAG, "%s shutter set: unknown speed '%s'", label, act.shutter_value);
            } else {
                LOGI(TAG, "%s shutter set: %s", label, act.shutter_value);
            }
        } else if (strcmp(cmd, "adjust") == 0) {
            bool faster = strcmp(act.shutter_value, "faster") == 0;
            shutter_measure_adjust_target(faster);
            LOGI(TAG, "%s shutter adjust: %s", label, act.shutter_value);
        } else if (strcmp(cmd, "toggle_lock") == 0) {
            if (!shutter_measure_toggle_lock()) {
                LOGW(TAG, "%s shutter toggle_lock: no target set", label);
            } else {
                LOGI(TAG, "%s shutter toggle_lock", label);
            }
        } else if (strcmp(cmd, "sess_start") == 0) {
            shutter_session_start(act.shutter_value);
            LOGI(TAG, "%s shutter sess_start: camera='%s'", label, act.shutter_value);
        } else if (strcmp(cmd, "sess_stop") == 0) {
            shutter_session_stop();
            LOGI(TAG, "%s shutter sess_stop", label);
        } else if (strcmp(cmd, "sess_toggle") == 0) {
            shutter_session_toggle(act.shutter_value);
            LOGI(TAG, "%s shutter sess_toggle: camera='%s'", label, act.shutter_value);
        } else if (strcmp(cmd, "sess_discard") == 0) {
            shutter_session_discard_last();
            LOGI(TAG, "%s shutter sess_discard", label);
        } else if (strcmp(cmd, "guide_start") == 0) {
            shutter_session_guide_start(act.shutter_value);
            LOGI(TAG, "%s shutter guide_start: test='%s'", label, act.shutter_value);
        } else if (strcmp(cmd, "guide_stop") == 0) {
            shutter_session_guide_stop();
            LOGI(TAG, "%s shutter guide_stop", label);
        } else if (strcmp(cmd, "guide_skip") == 0) {
            shutter_session_guide_skip();
            LOGI(TAG, "%s shutter guide_skip", label);
        } else if (strcmp(cmd, "guide_redo") == 0) {
            shutter_session_guide_redo();
            LOGI(TAG, "%s shutter guide_redo", label);
        } else if (strcmp(cmd, "align_start") == 0) {
            shutter_capture_start_alignment();
            LOGI(TAG, "%s shutter align_start", label);
        } else if (strcmp(cmd, "align_stop") == 0) {
            shutter_capture_stop_alignment();
            LOGI(TAG, "%s shutter align_stop", label);
        } else if (strcmp(cmd, "recalibrate") == 0) {
            shutter_capture_recalibrate();
            LOGI(TAG, "%s shutter recalibrate", label);
        } else {
            LOGW(TAG, "%s shutter: unknown cmd '%s'", label, cmd);
        }
#endif // IS_SHUTTER_TESTER
    } else if (strcmp(act.type, ACTION_TYPE_SYSTEM) == 0) {
        if (strcmp(act.system_command, "reboot") == 0) {
            LOGI(TAG, "%s system: reboot", label);
            delay(200);
            ESP.restart();
        } else if (strcmp(act.system_command, "wifi_reconnect") == 0) {
            LOGI(TAG, "%s system: wifi_reconnect", label);
            wifi_manager_request_reconnect();
        } else if (strcmp(act.system_command, "screensaver") == 0) {
            LOGI(TAG, "%s system: screensaver", label);
            screen_saver_manager_sleep_now();
        } else {
            LOGW(TAG, "%s system: unknown command '%s'", label, act.system_command);
        }
    } else {
        LOGW(TAG, "%s unknown action type: '%s'", label, act.type);
    }
}

// ---------------------------------------------------------------------------
// Called from main loop() — placeholder for future deferred operations.
// ---------------------------------------------------------------------------
void action_dispatch_loop() {
}

#endif // HAS_DISPLAY
