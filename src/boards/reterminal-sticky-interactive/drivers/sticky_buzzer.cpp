#include <Arduino.h>
#include "audio_output_driver.h"
#include "log_manager.h"

namespace {
constexpr uint8_t kBuzzerPin = 48;
constexpr uint8_t kResolution = 10;
constexpr uint32_t kMaxDuty = 1U << (kResolution - 1);

class StickyBuzzer : public AudioOutputDriver {
public:
    bool begin(uint32_t sample_rate) override {
        if (!sample_rate) return false;
        if (attached) return setNativeTone(0, 0);
        if (!ledcAttach(kBuzzerPin, 1000, kResolution)) {
            LOGE("Audio", "Buzzer LEDC attachment failed");
            return false;
        }
        attached = true;
        return setNativeTone(0, 0);
    }

    bool write(const int16_t*, size_t) override { return false; }
    bool supportsNativeTone() const override { return true; }
    bool setVolume(uint8_t value) override {
        volume = value > 100 ? 100 : value;
        return setNativeTone(frequency, gain);
    }
    bool setMuted(bool muted) override {
        return muted ? setNativeTone(0, 0) : attached;
    }
    bool setNativeTone(uint16_t hz, uint8_t gain_percent) override {
        if (!attached) return false;
        frequency = hz;
        gain = gain_percent > 100 ? 100 : gain_percent;
        const uint32_t duty = kMaxDuty * volume * gain / 10000;
        if (!hz || !duty) {
            if (ledcWrite(kBuzzerPin, 0)) return true;
        } else if (ledcChangeFrequency(kBuzzerPin, hz, kResolution) &&
                   ledcWrite(kBuzzerPin, duty)) {
            return true;
        }
        LOGE("Audio", "Buzzer LEDC output failed");
        if (!ledcWrite(kBuzzerPin, 0)) {
            LOGE("Audio", "Buzzer silence failed; detaching output");
            if (!end()) LOGE("Audio", "Buzzer emergency shutdown failed");
        }
        return false;
    }
    bool isAvailable() const override { return attached; }
    bool needsRebootShutdown() const override { return true; }
    bool end() override {
        if (!attached) return true;
        const bool silenced = ledcWrite(kBuzzerPin, 0);
        const bool detached = ledcDetach(kBuzzerPin);
        if (!silenced) LOGW("Audio", "Buzzer duty clear failed during shutdown");
        if (!detached) LOGE("Audio", "Buzzer detach failed");
        if (detached) {
            attached = false;
            pinMode(kBuzzerPin, OUTPUT);
            digitalWrite(kBuzzerPin, LOW);
        }
        return detached;
    }
private:
    bool attached = false;
    uint8_t volume = 0;
    uint8_t gain = 0;
    uint16_t frequency = 0;
};
}

AudioOutputDriver* audio_output_driver_create() {
    static StickyBuzzer driver;
    return &driver;
}
