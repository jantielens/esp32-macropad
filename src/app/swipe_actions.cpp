#include "swipe_actions.h"

#if HAS_DISPLAY

#include "action_dispatch.h"
#include "log_manager.h"
#include "swipe_config.h"
#if HAS_AUDIO
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

static void on_gesture(lv_event_t* e) {
    uint32_t now = lv_tick_get();
    if (now - s_last_swipe_ms < SWIPE_DEBOUNCE_MS) return;

    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());

    const SwipeConfig* cfg = swipe_config_get();
    const ButtonAction* act = nullptr;
    const char* dir_label = nullptr;

    switch (dir) {
        case LV_DIR_LEFT:  act = &cfg->swipe_left;  dir_label = "SwipeL"; break;
        case LV_DIR_RIGHT: act = &cfg->swipe_right; dir_label = "SwipeR"; break;
        case LV_DIR_TOP:   act = &cfg->swipe_up;    dir_label = "SwipeU"; break;
        case LV_DIR_BOTTOM:act = &cfg->swipe_down;  dir_label = "SwipeD"; break;
        default: return;
    }

    if (!act || !act->type[0]) return;

    s_last_swipe_ms = now;

    // Audio cue — use device tap_beep for swipes (skip if action is beep)
#if HAS_AUDIO
    if (strcmp(act->type, ACTION_TYPE_BEEP) != 0) {
        const char* pattern = device_config.tap_beep;
        if (pattern[0] && strcmp(pattern, "none") != 0) {
            audio_beep(pattern, 0);
        }
    }
#endif

    action_dispatch(*act, dir_label);

    // Suppress further touch events from this gesture (prevents multi-fire
    // when finger is still moving after screen transition)
    lv_indev_wait_release(lv_indev_active());
}

void swipe_actions_register(lv_obj_t* screen_obj) {
    if (!screen_obj) return;
    lv_obj_add_event_cb(screen_obj, on_gesture, LV_EVENT_GESTURE, nullptr);
    lv_obj_add_flag(screen_obj, LV_OBJ_FLAG_CLICKABLE);
}

#endif // HAS_DISPLAY
