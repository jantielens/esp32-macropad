#include "binding_builtin_schemes.h"
#include "board_config.h"

#include "audio_input_binding.h"
#include "voice_clips_binding.h"
#if HAS_BATTERY_INFO
#include "battery_binding.h"
#endif
#include "camera_binding.h"
#include "expr_binding.h"
#include "ext_binding.h"
#include "health_binding.h"
#include "sensor_binding.h"
#include "image_binding.h"
#include "list_binding.h"
#include "music_binding.h"
#include "mqtt_binding.h"
#include "ha_binding.h"
#include "net_binding.h"
#include "pad_binding.h"
#include "time_binding.h"
#include "timer_binding.h"
#include "alarm_binding.h"
#include "var_binding.h"

void binding_builtin_schemes_init() {
#if HAS_CAMERA && HAS_DISPLAY
    camera_binding_init();
#endif
#if HAS_IMAGE_LIBRARY
    image_binding_init();
#endif
    health_binding_init();
#if HAS_BATTERY_INFO
    battery_binding_init();
#endif
    sensor_binding_init();
    time_binding_init();
    expr_binding_init();
#if HAS_EXTERNAL_DATA
    external_binding_init();
#endif
#if HAS_DISPLAY && HAS_MQTT
    mqtt_binding_init();
#endif
#if HAS_HA_LIVE
    ha_binding_init();
#endif
#if HAS_DISPLAY
    pad_binding_init();
#endif
    timer_binding_init();
#if ALARM_ENABLED
    alarm_binding_init();
#endif
    music_binding_init();
#if HAS_AUDIO_INPUT && HAS_DISPLAY
    audio_input_binding_init();
#endif
#if HAS_VOICE_CLIPS && HAS_DISPLAY
    voice_clips_binding_init();
#endif
    list_binding_init();
    net_binding_init();
#if HAS_DISPLAY || HAS_BUTTON
    var_binding_init();
#endif
}