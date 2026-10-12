#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#include "../src/app/audio.cpp"

#if HAS_AUDIO
struct WorkerIdle {};
static TaskFunction_t worker = nullptr;
static std::function<void()> idle_hook;
static bool ota = false;
static uint32_t ota_epoch = 0;
static uint32_t owner_generation = 1;
static unsigned finished_generation = 0;
static bool owner_guard(uint32_t generation) { return generation == owner_generation; }
static void owner_finished(uint32_t generation) { finished_generation = generation; }
bool ota_activity_is_active() { return ota; }
uint32_t ota_activity_epoch() { return ota_epoch; }
int64_t esp_timer_get_time() { return 1; }
void device_telemetry_log_memory_snapshot(const char*) {}
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t) { return 16384; }
void vTaskDelay(TickType_t) {
    if (idle_hook) idle_hook();
    else throw WorkerIdle{};
}
bool rtos_create_task_internal_stack_pinned(TaskFunction_t function, const char*, uint32_t,
    void*, UBaseType_t, TaskHandle_t* handle, RtosTaskInternalAlloc*, BaseType_t) {
    worker = function;
    *handle = reinterpret_cast<TaskHandle_t>(1);
    return true;
}
QueueHandle_t xQueueCreate(size_t capacity, size_t item_size) {
    return new TestAudioQueue{capacity, item_size, {}};
}
BaseType_t xQueueReceive(QueueHandle_t queue, void* item, TickType_t) {
    if (queue->items.empty()) return pdFALSE;
    memcpy(item, queue->items.front().data(), queue->item_size);
    queue->items.pop_front();
    return pdTRUE;
}
BaseType_t xQueueSend(QueueHandle_t queue, const void* item, TickType_t) {
    if (queue->items.size() >= queue->capacity) return pdFALSE;
    const auto* bytes = static_cast<const uint8_t*>(item);
    queue->items.emplace_back(bytes, bytes + queue->item_size);
    return pdTRUE;
}
void vQueueDelete(QueueHandle_t queue) { delete queue; }

class TestOutput : public AudioOutputDriver {
public:
    size_t writes = 0;
    uint8_t volume = 0;
    bool fail = false;
    std::vector<int16_t> pcm;
    std::function<void()> on_write;
    bool begin(uint32_t) override { return true; }
    bool setVolume(uint8_t value) override { volume = value; return true; }
    bool setMuted(bool) override { return true; }
    bool isAvailable() const override { return true; }
    bool end() override { return true; }
    bool write(const int16_t* frames, size_t count) override {
        pcm.insert(pcm.end(), frames, frames + count * 2);
        ++writes;
        assert(writes < 1000);
        if (on_write) on_write();
        return !fail;
    }
};
static TestOutput output;
AudioOutputDriver* audio_output_driver_create() { return &output; }

#if HAS_SOUND_PLAYER
struct SoundPlayer {
    std::string path;
    size_t frames = 0;
    SoundPlayerPcmTransform transform = nullptr;
};
static unsigned decoder_count = 0;
static unsigned decoder_peak = 0;
static std::vector<std::string> opened;
static MusicCatalogSnapshot catalog = {};
bool music_catalog_store_init() { return true; }
MusicCatalogSnapshot* music_catalog_store_begin_build() { return &catalog; }
void music_catalog_store_publish(MusicCatalogResult) {}
const MusicCatalogSnapshot* music_catalog_store_active_for_audio() { return &catalog; }
bool music_catalog_store_copy(MusicCatalogSnapshot* out, MusicCatalogStatus*) { *out = catalog; return true; }
bool music_catalog_store_status(MusicCatalogStatus* out) {
    *out = {};
    out->available = true;
    out->count = catalog.count;
    return true;
}
bool music_catalog_discover(MusicCatalog* builder, MusicCatalogSnapshot* target) {
    builder->begin(target);
    builder->add("/media/first.mp3");
    builder->add("/media/second.mp3");
    builder->publish();
    return true;
}
bool sound_store_exists(const char* name) { return strcmp(name, "missing") != 0; }
char* sound_store_path(const char* name, char* path, size_t capacity) {
    snprintf(path, capacity, "/sounds/%s.mp3", name);
    return path;
}
SoundPlayer* sound_player_begin_path(AudioOutputDriver*, const char* path,
                                     SoundPlayerPcmTransform transform, void*) {
    if (strstr(path, "missing")) return nullptr;
    assert(decoder_count == 0);
    ++decoder_count;
    decoder_peak = std::max(decoder_peak, decoder_count);
    opened.emplace_back(path);
    return new SoundPlayer{path, 0, transform};
}
SoundPlayer* sound_player_begin_memory(AudioOutputDriver* driver, const uint8_t*, size_t) {
    return sound_player_begin_path(driver, "memory", nullptr, nullptr);
}
SoundPlayerStepResult sound_player_step(SoundPlayer* player) {
    if (strstr(player->path.c_str(), "corrupt")) return SOUND_PLAYER_STEP_ERROR;
    if (player->frames == 3) return SOUND_PLAYER_STEP_COMPLETE;
    int16_t frames[512 * 2];
    std::fill(std::begin(frames), std::end(frames), 10000);
    if (player->transform) player->transform(nullptr, frames, 512);
    AudioStarvationStats stats = {};
    if (!audio_write_with_stats(&output, frames, 512, &stats)) return SOUND_PLAYER_STEP_ERROR;
    ++player->frames;
    return SOUND_PLAYER_STEP_PLAYING;
}
bool sound_player_restart(SoundPlayer* player) {
    if (!player->frames) return false;
    player->frames = 0;
    return true;
}
bool sound_player_get_timing(const SoundPlayer* player, uint64_t* total, uint64_t* elapsed) {
    *total = 0;
    *elapsed = player->frames * 1000000;
    return true;
}
void sound_player_close(SoundPlayer* player) { --decoder_count; delete player; }
#endif

static void run_worker() {
    try { worker(nullptr); } catch (const WorkerIdle&) {}
}
static void reset() {
    ota = false;
    output = TestOutput{};
    idle_hook = nullptr;
    ++owner_generation;
    finished_generation = 0;
    audio_set_volume(70);
#if HAS_SOUND_PLAYER
    assert(decoder_count == 0);
    opened.clear();
    decoder_peak = 0;
#endif
}

int main() {
    audio_init(70);
    reset();
    assert(audio_submit_tone("400:20 20", 80, true, owner_guard, owner_generation, true, owner_finished));
    output.on_write = [] {
        if (output.writes == 1) {
            audio_feedback("1200:20");
            assert(audio_submit_tone("700:20", 0, false));
            assert(!audio_submit_tone("400:20 broken", 0, false));
            audio_set_volume(22);
        }
        if (output.writes == 10) audio_stop();
    };
    run_worker();
    assert(output.writes >= 10 && finished_generation == owner_generation);
    assert(!audio_is_playing());
    assert(output.volume == 22);
    assert(std::any_of(output.pcm.begin() + 960 * 2, output.pcm.begin() + 1024 * 2,
        [](int16_t sample) { return sample != 0; }));

    reset();
    audio_feedback("1000:10000");
    run_worker();
    assert(output.writes == 24);

    reset();
    assert(audio_submit_tone("400:20", 0, true));
    audio_feedback("1000:20");
    audio_stop_all();
    run_worker();
    assert(output.writes == 0);

    reset();
    assert(audio_submit_tone("400:20", 0, true, owner_guard, owner_generation));
    output.on_write = [] {
        if (output.writes == 1) audio_stop_owned(owner_guard, owner_generation - 1);
        if (output.writes == 5) ++owner_generation;
    };
    run_worker();
    assert(output.writes == 5);

    reset();
    const AudioPlaybackGuard replacement_guard = [](uint32_t) { return true; };
    assert(audio_submit_tone("400:20", 0, true, replacement_guard, 1));
    output.on_write = [replacement_guard] {
        if (output.writes == 1) assert(audio_submit_tone("800:20", 0, true, replacement_guard, 2));
        if (output.writes == 2) audio_stop_owned(replacement_guard, 1);
        if (output.writes == 5) audio_stop_owned(replacement_guard, 2);
    };
    run_worker();
    assert(output.writes == 5);

    reset();
    assert(audio_submit_tone("400:20", 0, true));
    output.on_write = [] {
        if (output.writes == 1) audio_feedback("1200:10000");
        if (output.writes == 3) { ota = true; ++ota_epoch; }
    };
    run_worker();
    assert(output.writes == 3 && !audio_is_playing());

#if HAS_SOUND_PLAYER
    reset();
    assert(audio_music_command(MUSIC_COMMAND_PLAY_PAUSE) == AUDIO_MUSIC_SUBMIT_QUEUED);
    output.on_write = [] {
        if (output.writes == 1) assert(audio_submit_sound("alert", 0));
    };
    run_worker();
    assert((opened == std::vector<std::string>{"/media/first.mp3", "/sounds/alert.mp3", "/media/first.mp3", "/media/second.mp3"}));
    assert(decoder_peak == 1);

    reset();
    assert(audio_music_command(MUSIC_COMMAND_PLAY_PAUSE) == AUDIO_MUSIC_SUBMIT_QUEUED);
    output.on_write = [] {
        if (output.writes == 1) assert(audio_submit_sound("alarm", 0, owner_guard, owner_generation, true));
        if (output.writes == 3) {
            audio_feedback("1200:20");
            assert(audio_submit_sound("ignored", 0));
            assert(audio_music_command(MUSIC_COMMAND_PLAY_PAUSE) == AUDIO_MUSIC_SUBMIT_QUEUED);
        }
        if (output.writes == 10) audio_stop();
    };
    run_worker();
    assert((opened == std::vector<std::string>{"/media/first.mp3", "/sounds/alarm.mp3"}));
    AudioMusicInfo info = {};
    audio_get_music_info(&info);
    assert(info.status == AUDIO_MUSIC_PAUSED && decoder_peak == 1);

    reset();
    assert(audio_alarm_session(owner_guard, owner_generation));
    assert(audio_submit_sound("missing", 0, owner_guard, owner_generation, false, true));
    assert(audio_music_command(MUSIC_COMMAND_PLAY_PAUSE) == AUDIO_MUSIC_SUBMIT_QUEUED);
    unsigned idle_count = 0;
    idle_hook = [&] {
        audio_get_music_info(&info);
        assert(info.status == AUDIO_MUSIC_INTERRUPTED);
        if (++idle_count == 1) {
            audio_stop();
            assert(audio_submit_tone("700:20", 0, false));
        } else { ++owner_generation; idle_hook = nullptr; }
    };
    run_worker();
    assert(idle_count == 2 && decoder_peak == 1);
    assert(opened.back() == "/media/second.mp3");

    reset();
    assert(audio_music_command(MUSIC_COMMAND_PLAY_PAUSE) == AUDIO_MUSIC_SUBMIT_QUEUED);
    output.on_write = [] {
        if (output.writes == 1) assert(audio_submit_sound("alert", 0));
        if (output.writes == 3) audio_stop_all();
    };
    run_worker();
    assert(output.writes == 3);
    audio_get_music_info(&info);
    assert(info.status == AUDIO_MUSIC_STOPPED);

    reset();
    assert(audio_music_command(MUSIC_COMMAND_PLAY_PAUSE) == AUDIO_MUSIC_SUBMIT_QUEUED);
    output.on_write = [] {
        if (output.writes == 1) assert(audio_submit_sound("alert", 0));
        if (output.writes == 2) assert(audio_music_command(MUSIC_COMMAND_STOP) == AUDIO_MUSIC_SUBMIT_QUEUED);
    };
    run_worker();
    assert((opened == std::vector<std::string>{"/media/first.mp3", "/sounds/alert.mp3"}));
    assert(output.writes == 4);
    audio_get_music_info(&info);
    assert(info.status == AUDIO_MUSIC_STOPPED);

    reset();
    assert(audio_music_command(MUSIC_COMMAND_PLAY_PAUSE) == AUDIO_MUSIC_SUBMIT_QUEUED);
    output.on_write = [] {
        if (output.writes == 1) assert(audio_submit_sound("alert", 0));
        if (output.writes == 2) {
            MusicCatalog replacement;
            replacement.begin(&catalog);
            replacement.add("/media/second.mp3");
            replacement.publish();
        }
    };
    run_worker();
    assert((opened == std::vector<std::string>{"/media/first.mp3", "/sounds/alert.mp3"}));
    audio_get_music_info(&info);
    assert(info.status == AUDIO_MUSIC_ERROR);

    reset();
    assert(audio_music_command(MUSIC_COMMAND_PLAY_PAUSE) == AUDIO_MUSIC_SUBMIT_QUEUED);
    output.on_write = [] {
        if (output.writes == 1) assert(audio_submit_sound("corrupt", 0));
    };
    run_worker();
    assert((opened == std::vector<std::string>{"/media/first.mp3", "/sounds/corrupt.mp3", "/media/first.mp3", "/media/second.mp3"}));
    assert(decoder_peak == 1);
#endif
    std::puts("audio worker policy: PASS");
}
#else
int main() { std::puts("audio disabled: PASS"); }
#endif