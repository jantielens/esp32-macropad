#include "board_config.h"

#if HAS_AUDIO

#include <Arduino.h>
#include "audio_output_driver.h"
#include "driver/gpio.h"
#ifdef BOARD_HARDWARE_HEADER
#include BOARD_HARDWARE_HEADER
#endif

#ifndef BOARD_AUDIO_CONTROL_FACTORY
static bool gpio_audio_mute(bool muted) {
    if (AUDIO_PA_PIN < 0) return false;
    pinMode(AUDIO_PA_PIN, OUTPUT);
    const bool enabled = !muted;
    digitalWrite(AUDIO_PA_PIN, (enabled ^ AUDIO_PA_ACTIVE_LOW) ? HIGH : LOW);
    return true;
}
#endif

AudioBoardControl audio_board_control_create() {
#ifdef BOARD_AUDIO_CONTROL_FACTORY
    return BOARD_AUDIO_CONTROL_FACTORY();
#else
    AudioBoardControl control;
    if (AUDIO_PA_PIN >= 0) control.mute = gpio_audio_mute;
    return control;
#endif
}

#if AUDIO_OUTPUT_DRIVER == AUDIO_OUTPUT_DRIVER_ES8311
#include "drivers/es8311_audio_driver.cpp"
#elif AUDIO_OUTPUT_DRIVER == AUDIO_OUTPUT_DRIVER_PCM510XA
#include "drivers/pcm510xa_audio_driver.cpp"
#else
#error "No audio output driver selected or unknown driver type"
#endif

#endif // HAS_AUDIO