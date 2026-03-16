#include "pad_screen.h"
#include "../display_manager.h"
#include "../log_manager.h"
#if HAS_MQTT
#include "../mqtt_manager.h"
#include <ArduinoJson.h>
#endif
#if HAS_BLE_HID
#include "../ble_hid.h"
#endif

// TAG and TAP_FLASH_DURATION_MS are defined in pad_screen.cpp which is
// #included before this file in screens.cpp.

// ============================================================================
// Event Handlers
// ============================================================================

// Tap flash: show overlay briefly, then hide after timeout
void PadScreen::tapFlashTimerCb(lv_timer_t* timer) {
    lv_obj_t* overlay = (lv_obj_t*)lv_timer_get_user_data(timer);
    if (overlay) {
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    }
    lv_timer_delete(timer);
}

static void do_tap_flash(ButtonTile* tile) {
    if (!tile->tap_overlay) return;
    lv_obj_remove_flag(tile->tap_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_timer_create(PadScreen::tapFlashTimerCb, TAP_FLASH_DURATION_MS, tile->tap_overlay);
}

// Execute a typed action (dispatch on action.type)
static void execute_action(const ButtonAction& act, const char* label) {
    if (!act.type[0]) return; // No action

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
        LOGW(TAG, "%s mqtt: not compiled (HAS_MQTT=false)", label);
#endif
    } else if (strcmp(act.type, ACTION_TYPE_KEY) == 0) {
#if HAS_BLE_HID
        if (!ble_hid_is_initialized()) {
            LOGW(TAG, "%s key: BLE disabled in settings", label);
        } else if (act.key_sequence[0]) {
            LOGI(TAG, "%s key: '%s'", label, act.key_sequence);
            ble_hid_request_sequence(act.key_sequence);
        } else {
            LOGW(TAG, "%s key: empty sequence", label);
        }
#else
        LOGW(TAG, "%s key: not compiled (HAS_BLE_HID=false)", label);
#endif
    } else if (strcmp(act.type, ACTION_TYPE_BLE_PAIR) == 0) {
#if HAS_BLE_HID
        if (!ble_hid_is_initialized()) {
            LOGW(TAG, "%s ble_pair: BLE disabled in settings", label);
        } else {
            LOGI(TAG, "%s ble_pair: starting live re-pairing", label);
            ble_hid_request_pairing();
        }
#else
        LOGW(TAG, "%s ble_pair: not compiled (HAS_BLE_HID=false)", label);
#endif
    } else {
        LOGW(TAG, "%s unknown action type: '%s'", label, act.type);
    }
}

// Publish HA event entity payload for a button press/hold
#if HAS_MQTT
static void publish_button_event(const ButtonTile* tile, const char* event_type) {
    if (!mqtt_manager.connected()) return;

    char topic[128];
    snprintf(topic, sizeof(topic), "%s/event", mqtt_manager.baseTopic());

    StaticJsonDocument<256> doc;
    doc["event_type"] = event_type;
    doc["page"] = tile->page;
    doc["col"] = tile->col;
    doc["row"] = tile->row;

    // Pick the most prominent label text
    const char* label = "";
    if (tile->label_center) label = lv_label_get_text(tile->label_center);
    if ((!label || !label[0]) && tile->label_top) label = lv_label_get_text(tile->label_top);
    if ((!label || !label[0]) && tile->label_bottom) label = lv_label_get_text(tile->label_bottom);
    if (label && label[0]) doc["label"] = label;

    mqtt_manager.publishJson(topic, doc, false);
}
#endif

void PadScreen::onTap(lv_event_t* e) {
    ButtonTile* tile = (ButtonTile*)lv_event_get_user_data(e);
    if (!tile || !tile->obj) return;

    // Tap flash
    do_tap_flash(tile);

    execute_action(tile->action, "Tap");

#if HAS_MQTT
    publish_button_event(tile, "press");
#endif
}

void PadScreen::onLongPress(lv_event_t* e) {
    ButtonTile* tile = (ButtonTile*)lv_event_get_user_data(e);
    if (!tile || !tile->obj) return;

    // Tap flash
    do_tap_flash(tile);

    execute_action(tile->lp_action, "LP");

#if HAS_MQTT
    publish_button_event(tile, "hold");
#endif
}

void PadScreen::onSwipe(lv_event_t* e) {
    PadScreen* self = (PadScreen*)lv_event_get_user_data(e);
    if (!self) return;

    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());

    // Swipe left/right to move between pads
    int next_page = -1;
    if (dir == LV_DIR_LEFT && self->pageIndex < MAX_PADS - 1) {
        next_page = self->pageIndex + 1;
    } else if (dir == LV_DIR_RIGHT && self->pageIndex > 0) {
        next_page = self->pageIndex - 1;
    }

    if (next_page >= 0) {
        char screen_id[16];
        snprintf(screen_id, sizeof(screen_id), "pad_%d", next_page);
        bool ok = false;
        display_manager_show_screen(screen_id, &ok);
        if (ok) {
            LOGD(TAG, "Swipe to pad_%d", next_page);
        }
    }
}
