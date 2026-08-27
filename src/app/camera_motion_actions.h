#pragma once

#include "board_config.h"

#define CAMERA_MOTION_ACTIONS_ENABLED (HAS_CAMERA && (HAS_DISPLAY || HAS_BUTTON))

#if CAMERA_MOTION_ACTIONS_ENABLED

#include "pad_config.h"

#include <ArduinoJson.h>

struct CameraMotionActionsConfig {
    ButtonAction actions[MAX_BUTTON_ACTIONS];
    uint8_t action_count;
};

void camera_motion_actions_init();
void camera_motion_actions_on_presence_started();
void camera_motion_actions_to_json(JsonArray actions);
bool camera_motion_actions_save_raw(const uint8_t* json, size_t len);
#if HAS_MQTT
void camera_motion_actions_collect_binding_topics(void* user_data);
#endif

#endif // CAMERA_MOTION_ACTIONS_ENABLED