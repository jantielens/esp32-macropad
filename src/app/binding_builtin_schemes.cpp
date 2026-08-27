#include "binding_builtin_schemes.h"

#include "audio_input_binding.h"
#include "camera_binding.h"
#include "expr_binding.h"
#include "health_binding.h"
#include "list_binding.h"
#include "music_binding.h"
#include "net_binding.h"
#include "pad_binding.h"
#include "time_binding.h"
#include "timer_binding.h"

void binding_builtin_schemes_init() {
#if HAS_CAMERA && HAS_DISPLAY
    camera_binding_init();
#endif
    health_binding_init();
    time_binding_init();
    expr_binding_init();
#if HAS_DISPLAY && HAS_MQTT
    pad_binding_init();
#endif
    timer_binding_init();
    music_binding_init();
#if HAS_AUDIO_INPUT && HAS_DISPLAY
    audio_input_binding_init();
#endif
    list_binding_init();
    net_binding_init();
}