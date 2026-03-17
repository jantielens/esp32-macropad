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
#include "config_manager.h"
#include "web_portal_state.h"
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
        // Persist to NVS
        DeviceConfig *cfg = web_portal_get_current_config();
        if (cfg) {
            cfg->audio_volume = audio_get_volume();
            config_manager_save(cfg);
        }
#else
        LOGW(TAG, "%s volume: not compiled", label);
#endif
    } else {
        LOGW(TAG, "%s unknown action type: '%s'", label, act.type);
    }
}

#endif // HAS_DISPLAY
