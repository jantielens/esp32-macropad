#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
using std::min;
#include "../src/app/sound_player.cpp"

std::map<std::string, std::string> timer_test_files;
bool timer_test_fail_open = false;
size_t timer_test_write_limit = SIZE_MAX;
FakeLittleFS LittleFS;
static bool ota = false;
static bool guard_valid = true;
static volatile bool stopped = false;
bool ota_activity_is_active() { return ota; }
static bool playback_guard(uint32_t generation) { return guard_valid && generation == 7; }
void AudioOutputDriver::setMuted(bool) {}
void audio_log_starvation(const AudioStarvationStats&) {}
char* sound_store_path(const char* name, char* output, size_t capacity) {
    snprintf(output, capacity, "/sounds/%s.mp3", name);
    return output;
}

class TestOutput : public AudioOutputDriver {
public:
    size_t frames = 0;
    size_t interrupt_after = SIZE_MAX;
    unsigned interruption = 0;
    bool fail = false;
    bool begin(uint32_t) override { return true; }
    void setVolume(uint8_t) override {}
    bool write(const int16_t*, size_t count) override {
        frames += count;
        if (frames >= interrupt_after) {
            if (interruption == 1) stopped = true;
            if (interruption == 2) guard_valid = false;
            if (interruption == 3) ota = true;
        }
        return !fail;
    }
};
bool audio_write_with_stats(AudioOutputDriver* driver, const int16_t* frames,
                            size_t count, AudioStarvationStats*) {
    return driver->write(frames, count);
}

int main() {
    uint64_t duration_us = 0;
    assert(sound_player_duration_add(&duration_us, 1152, 44100));
    assert(duration_us > 0);
    std::string frame(417, '\0');
    frame[0] = char(0xff);
    frame[1] = char(0xfb);
    frame[2] = char(0x90);
    timer_test_files["/sounds/clip.mp3"] = frame + frame + frame;
    TestOutput output;
    assert(sound_player_play(&output, "clip", &stopped, playback_guard, 7));
    const size_t clip_frames = output.frames;
    assert(clip_frames > 0);
    for (unsigned interruption = 1; interruption <= 3; ++interruption) {
        stopped = ota = false;
        guard_valid = true;
        output = TestOutput();
        output.interrupt_after = clip_frames + 1;
        output.interruption = interruption;
        sound_player_play(&output, "clip", &stopped, playback_guard, 7, true);
        assert(output.frames > clip_frames && output.frames <= clip_frames * 2);
        assert(interruption != 1 || stopped);
        assert(interruption != 2 || !guard_valid);
        assert(interruption != 3 || ota);
    }
    stopped = ota = false;
    guard_valid = true;
    output = TestOutput();
    output.fail = true;
    assert(!sound_player_play(&output, "clip", &stopped, playback_guard, 7, true));
    output = TestOutput();
    timer_test_files["/sounds/empty.mp3"] = "";
    assert(!sound_player_play(&output, "empty", &stopped, playback_guard, 7, true));
    timer_test_files["/sounds/corrupt.mp3"] = std::string(100, '\0');
    assert(!sound_player_play(&output, "corrupt", &stopped, playback_guard, 7, true));
    assert(!sound_player_play(&output, "missing", &stopped, playback_guard, 7, true));
    assert(output.frames == 0);
    std::puts("sound player loop: PASS");
}