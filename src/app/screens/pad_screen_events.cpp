#include "pad_screen.h"
#include "../action_dispatch.h"
#include "../display_manager.h"
#include "../log_manager.h"
#include "../swipe_actions.h"
#if HAS_AUDIO
#include "../audio.h"
#include "../config_manager.h"
extern DeviceConfig device_config;
#endif
#if HAS_MQTT
#include "../mqtt_manager.h"
#include <ArduinoJson.h>
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

    // Suppress taps that LVGL fires as part of a swipe gesture
    if (lv_tick_get() - swipe_actions_last_swipe_time() < 300) return;

    // Visual and audio cues only when an action is configured
    if (tile->action.type[0]) {
        do_tap_flash(tile);
#if HAS_AUDIO
        if (strcmp(tile->action.type, ACTION_TYPE_BEEP) != 0) {
            const char* pattern = tile->tap_beep[0] ? tile->tap_beep : device_config.tap_beep;
            if (pattern[0] && strcmp(pattern, "none") != 0) {
                audio_beep(pattern, 0);
            }
        }
#endif
    }

    action_dispatch(tile->action, "Tap");

#if HAS_MQTT
    publish_button_event(tile, "press");
#endif
}

void PadScreen::onLongPress(lv_event_t* e) {
    ButtonTile* tile = (ButtonTile*)lv_event_get_user_data(e);
    if (!tile || !tile->obj) return;

    // Suppress long-press that LVGL fires as part of a swipe gesture
    if (lv_tick_get() - swipe_actions_last_swipe_time() < 300) return;

    // Visual and audio cues only when an action is configured
    if (tile->lp_action.type[0]) {
        do_tap_flash(tile);
#if HAS_AUDIO
        if (strcmp(tile->lp_action.type, ACTION_TYPE_BEEP) != 0) {
            const char* pattern = tile->lp_beep[0] ? tile->lp_beep : device_config.lp_beep;
            if (pattern[0] && strcmp(pattern, "none") != 0) {
                audio_beep(pattern, 0);
            }
        }
#endif
    }

    action_dispatch(tile->lp_action, "LP");

#if HAS_MQTT
    publish_button_event(tile, "hold");
#endif
}

