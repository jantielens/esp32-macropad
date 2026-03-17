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
    } else {
        LOGW(TAG, "%s unknown action type: '%s'", label, act.type);
    }
}

#endif // HAS_DISPLAY
