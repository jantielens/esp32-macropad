#include "board_config.h"

#if HAS_AUDIO

#include <Arduino.h>
#include "audio_output_driver.h"
#include "driver/gpio.h"

void AudioOutputDriver::setMuted(bool muted) {
    if (AUDIO_PA_PIN < 0) return;
    pinMode(AUDIO_PA_PIN, OUTPUT);
    const bool enabled = !muted;
    digitalWrite(AUDIO_PA_PIN, (enabled ^ AUDIO_PA_ACTIVE_LOW) ? HIGH : LOW);
}

#if AUDIO_OUTPUT_DRIVER == AUDIO_OUTPUT_DRIVER_ES8311
#include "drivers/es8311_audio_driver.cpp"
#elif AUDIO_OUTPUT_DRIVER == AUDIO_OUTPUT_DRIVER_PCM510XA
#include "drivers/pcm510xa_audio_driver.cpp"
#else
#error "No audio output driver selected or unknown driver type"
#endif

#endif // HAS_AUDIO