#include <cstdio>
#include <cstdlib>

#include "audio_pcm_level.h"
#include "device_classes/voice_assistant/voice_auto_stop.h"

static void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

int main() {
    const int16_t full_scale[] = {32767, 32767, 32767, 32767};
    const AudioPcmLevels levels = audio_pcm_levels(full_scale, 2, 2);
    check(levels.rms == 100, "combined PCM helper uses meter RMS scale");
    check(levels.peak == 100, "combined PCM helper uses meter peak scale");

    VoiceAutoStopDetector detector = {};
    voice_auto_stop_init(&detector, 1000, 2);

    check(!voice_auto_stop_update(&detector, 0, 20000), "initial silence does not stop recording");
    check(!detector.speech_detected, "initial silence does not count as speech");

    check(!voice_auto_stop_update(&detector, 2, 20), "threshold-level speech starts listening");
    check(detector.speech_detected, "speech is detected");
    check(!voice_auto_stop_update(&detector, 0, 600), "short trailing silence continues recording");
    check(!voice_auto_stop_update(&detector, 3, 20), "speech resets trailing silence");
    check(!voice_auto_stop_update(&detector, 0, 999), "silence below timeout continues recording");
    check(voice_auto_stop_update(&detector, 0, 1), "silence at timeout stops recording");

    voice_auto_stop_init(&detector, 1000, 0);
    check(!voice_auto_stop_update(&detector, 0, 10), "zero threshold still treats silence as silence");
    check(!voice_auto_stop_update(&detector, 1, 10), "lowest audible level starts listening");

    std::puts("Voice auto-stop checks passed");
    return 0;
}