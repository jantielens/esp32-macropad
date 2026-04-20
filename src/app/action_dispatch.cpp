#include "action_dispatch.h"

#if HAS_DISPLAY

#include "display_manager.h"
#include "log_manager.h"
#include "message_bubble.h"

#if HAS_MQTT
#include "mqtt_manager.h"
#include "binding_template.h"
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

#include <math.h>

#define TAG "Action"

// Compute a clamped percentage value from a string, optionally as a delta from current.
static uint8_t compute_clamped_percent(const char* value_str, uint8_t current, bool is_adjust, int floor) {
    int v = is_adjust ? (int)current + lroundf(atof(value_str)) : lroundf(atof(value_str));
    if (v > 100) v = 100;
    if (v < floor) v = floor;
    return (uint8_t)v;
}

void action_dispatch(const ButtonAction& act, const char* label) {
    if (!act.type[0]) return;

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
        display_manager_set_backlight_brightness(nv);
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
        // Resolve all bindable fields before building params struct
        MessageBubbleParams params = {};

        // Resolve a bindable string field: binding → resolve, plain → copy, empty → default.
        auto resolve_field = [](const char* field, const char* def, char* out, size_t len) {
#if HAS_MQTT
            if (field[0] && binding_template_has_bindings(field)) {
                binding_template_resolve(field, out, len);
            } else
#endif
            {
                strlcpy(out, field[0] ? field : def, len);
            }
        };

        resolve_field(act.notify_text, "", params.text, sizeof(params.text));

        if (!params.text[0]) {
            message_bubble_dismiss();
            LOGI(TAG, "%s notify: dismiss", label);
        } else {
            // Duration
            char buf[16];
            resolve_field(act.notify_duration_ms, "3000", buf, sizeof(buf));
            params.duration_ms = (uint16_t)atoi(buf);

            // Colors
            char cbuf[CONFIG_BINDABLE_SHORT_LEN];
            resolve_field(act.notify_text_color, "#ffffff", cbuf, sizeof(cbuf));
            if (!parse_hex_color(cbuf, &params.text_color)) params.text_color = 0xFFFFFF;

            resolve_field(act.notify_bg_color, "#333333", cbuf, sizeof(cbuf));
            if (!parse_hex_color(cbuf, &params.bg_color)) params.bg_color = 0x333333;

            if (act.notify_border_color[0]) {
                resolve_field(act.notify_border_color, "", cbuf, sizeof(cbuf));
                params.has_border = parse_hex_color(cbuf, &params.border_color);
            }

            params.opacity = act.notify_opacity;
            params.font_size = act.notify_font_size;
            params.location = notify_location_from_str(act.notify_location);

            message_bubble_show(&params);
            LOGI(TAG, "%s notify: '%s' dur=%u loc=%s", label, params.text,
                 params.duration_ms, act.notify_location[0] ? act.notify_location : "bottom");
        }
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
