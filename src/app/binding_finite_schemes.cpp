#include "binding_finite_schemes.h"

#include "audio_input_binding.h"
#include "health_binding.h"
#include "music_binding.h"
#include "net_binding.h"
#include "timer_binding.h"

void binding_finite_schemes_init() {
    health_binding_init();
    timer_binding_init();
    music_binding_init();
#if HAS_AUDIO_INPUT && HAS_DISPLAY
    audio_input_binding_init();
#endif
    net_binding_init();
}