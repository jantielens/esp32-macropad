#include "action_dispatch.h"

#if HAS_DISPLAY

#include "display_manager.h"
#include "log_manager.h"

#if HAS_MQTT
#include "mqtt_manager.h"
#endif
#if HAS_BLE_HID
#include "ble_hid.h"
#endif
#if HAS_AUDIO
#include "audio.h"
#endif

#include "timer_engine.h"
#if IS_DARKROOM_TIMER
#include "expose_timer.h"
#include "test_strip.h"
#endif

#define TAG "Action"

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
        if (strcmp(act.volume_mode, "up") == 0) {
            uint8_t v = audio_get_volume();
            v = (v > 90) ? 100 : v + 10;
            audio_set_volume(v);
            LOGI(TAG, "%s volume up -> %u%%", label, v);
        } else if (strcmp(act.volume_mode, "down") == 0) {
            uint8_t v = audio_get_volume();
            v = (v < 10) ? 0 : v - 10;
            audio_set_volume(v);
            LOGI(TAG, "%s volume down -> %u%%", label, v);
        } else {
            uint8_t v = act.volume_value;
            if (v > 100) v = 100;
            audio_set_volume(v);
            LOGI(TAG, "%s volume set -> %u%%", label, v);
        }
#else
        LOGW(TAG, "%s volume: not compiled", label);
#endif
    } else if (strcmp(act.type, ACTION_TYPE_BRIGHTNESS) == 0) {
        uint8_t step = act.brightness_value > 0 ? act.brightness_value : 10;
        if (strcmp(act.brightness_mode, "up") == 0) {
            uint8_t v = display_manager_get_backlight_brightness();
            v = (v + step > 100) ? 100 : v + step;
            display_manager_set_backlight_brightness(v);
            LOGI(TAG, "%s brightness up -> %u%%", label, v);
        } else if (strcmp(act.brightness_mode, "down") == 0) {
            uint8_t v = display_manager_get_backlight_brightness();
            v = (v < step) ? 0 : v - step;
            display_manager_set_backlight_brightness(v);
            LOGI(TAG, "%s brightness down -> %u%%", label, v);
        } else {
            uint8_t v = act.brightness_value;
            if (v > 100) v = 100;
            display_manager_set_backlight_brightness(v);
            LOGI(TAG, "%s brightness set -> %u%%", label, v);
        }
    } else if (strcmp(act.type, ACTION_TYPE_TIMER) == 0) {
        // Payload format: "N:command" or "N:command:arg"
        // e.g. "1:toggle", "2:start", "1:adjust:30"
        const char* p = act.mqtt_payload;
        if (p && p[0] >= '1' && p[0] <= '0' + TIMER_COUNT && p[1] == ':') {
            uint8_t tid = p[0] - '0';
            const char* cmd = p + 2;

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
            } else if (strncmp(cmd, "adjust:", 7) == 0) {
                int32_t delta = (int32_t)atoi(cmd + 7);
                timer_adjust(tid, delta);
            } else {
                LOGW(TAG, "%s timer: unknown cmd '%s'", label, cmd);
            }
            LOGI(TAG, "%s timer: %c:%s", label, p[0], cmd);
        } else {
            LOGW(TAG, "%s timer: bad payload '%s'", label, p ? p : "(null)");
        }
#if IS_DARKROOM_TIMER
    } else if (strcmp(act.type, ACTION_TYPE_EXPOSE) == 0) {
        const char* cmd = act.mqtt_payload;
        if (cmd && cmd[0]) {
            LOGI(TAG, "%s expose: '%s'", label, cmd);
            expose_timer_dispatch(cmd);
        } else {
            LOGW(TAG, "%s expose: empty command", label);
        }
    } else if (strcmp(act.type, ACTION_TYPE_STRIP) == 0) {
        const char* cmd = act.mqtt_payload;
        if (cmd && cmd[0]) {
            LOGI(TAG, "%s strip: '%s'", label, cmd);
            test_strip_dispatch(cmd);
        } else {
            LOGW(TAG, "%s strip: empty command", label);
        }
#endif
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
