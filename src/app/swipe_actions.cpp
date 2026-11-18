#include "swipe_actions.h"

#if HAS_DISPLAY

#include "action_list.h"
#include "action_storage.h"
#include "log_manager.h"
#include "swipe_config.h"
#if HAS_AUDIO
#include "action_registry.h"
#include "audio.h"
#include "config_manager.h"
extern DeviceConfig device_config;
#endif

#define TAG "Swipe"

// Minimum interval between swipe actions (ms) — prevents multi-fire
#define SWIPE_DEBOUNCE_MS 300

static uint32_t s_last_swipe_ms = 0;

uint32_t swipe_actions_last_swipe_time() {
    return s_last_swipe_ms;
}

static void on_gesture(lv_event_t*) {
    uint32_t now = lv_tick_get();
    if (now - s_last_swipe_ms < SWIPE_DEBOUNCE_MS) return;

    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());

    SwipeDirection direction;
    const char* dir_label = nullptr;

    switch (dir) {
        case LV_DIR_LEFT:  direction = SWIPE_LEFT;  dir_label = "SwipeL"; break;
        case LV_DIR_RIGHT: direction = SWIPE_RIGHT; dir_label = "SwipeR"; break;
        case LV_DIR_TOP:   direction = SWIPE_UP;    dir_label = "SwipeU"; break;
        case LV_DIR_BOTTOM:direction = SWIPE_DOWN;  dir_label = "SwipeD"; break;
        default: return;
    }

    ButtonAction act = {};
    ActionStorage storage = {};
    if (!swipe_config_copy_action(direction, act, storage)) {
        LOGE(TAG, "Cannot copy %s action", dir_label);
        action_storage_clear(storage);
        return;
    }
    if (!act.type[0]) {
        action_storage_clear(storage);
        return;
    }

    s_last_swipe_ms = now;

    // Audio cue — use device tap_beep for swipes (skip if action produces audio)
#if HAS_AUDIO
    const ActionTypeDef* owner = nullptr;
    if (act.type[0] == '@') action_operation_for_action(act, &owner);
    const char* type = owner ? owner->type_name : act.type;
    if (strcmp(type, ACTION_TYPE_SOUND_ALERT) != 0) {
        const char* pattern = device_config.tap_beep;
        if (pattern[0] && strcmp(pattern, "none") != 0) {
            audio_feedback(pattern);
        }
    }
#endif

    action_list_dispatch(&act, 1, dir_label, ACTION_CONTINUATION_OWNER_LVGL, true);
    action_storage_clear(storage);

    // Suppress further touch events from this gesture (prevents multi-fire
    // when finger is still moving after screen transition)
    lv_indev_wait_release(lv_indev_active());
}

void swipe_actions_register(lv_obj_t* screen_obj, bool enabled) {
    if (!screen_obj) return;
    lv_obj_remove_event_cb(screen_obj, on_gesture);
    if (enabled) lv_obj_add_event_cb(screen_obj, on_gesture, LV_EVENT_GESTURE, nullptr);
    lv_obj_add_flag(screen_obj, LV_OBJ_FLAG_CLICKABLE);
}

#endif // HAS_DISPLAY
