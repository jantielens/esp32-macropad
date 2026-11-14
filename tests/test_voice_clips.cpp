#include "voice_clips.h"
#include "voice_clips_file.h"
#include "audio_input.h"
#include "rtos_task_utils.h"
#include "fs_health.h"
#include "storage_browser.h"
#include "voice_wav.h"
#include <LittleFS.h>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>

FakeIndexedStorage LittleFS;
#if USE_SD_STORAGE
FakeIndexedStorage& SD_MMC = LittleFS;
#endif

namespace {
std::mutex completion_mutex;
std::condition_variable completion_cv;
std::map<uint32_t, bool> completions;
std::atomic<bool> owned{false};
std::atomic<bool> expired{false};
std::atomic<bool> block_start{false};
std::atomic<bool> start_entered{false};
std::atomic<bool> force_overrun{false};
std::atomic<bool> no_audio{false};
std::atomic<bool> block_read{false};
std::atomic<bool> read_entered{false};
std::atomic<bool> block_publication{false};
std::atomic<bool> publication_entered{false};
std::atomic<unsigned> reads{0};
std::atomic<unsigned> pauses{0};
std::atomic<bool> block_capacity{false};
std::atomic<bool> capacity_entered{false};
std::atomic<bool> block_write{false};
std::atomic<bool> write_entered{false};
std::atomic<bool> fail_stop{false};
std::atomic<bool> block_stop{false};
std::atomic<bool> stop_entered{false};
bool no_psram = false;
bool capture_psram_stack = false;
bool capture_internal_stack = false;

void usage_hook() {
    if (!block_capacity) return;
    capacity_entered = true;
    while (block_capacity) std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

void write_hook(const char* path, size_t length) {
    if (!block_write || length <= VOICE_WAV_HEADER_BYTES || !strstr(path, ".wav.tmp")) return;
    write_entered = true;
    while (block_write) std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

void publication_hook(const char* from, const char*) {
    if (!strstr(from, ".wav.tmp")) return;
#if USE_SD_STORAGE && HAS_AUTO_SHUTDOWN
    assert(!ota_activity_try_begin());
    assert(!ota_activity_try_begin_network_transition());
#endif
    publication_entered = true;
    while (block_publication) std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

void publication_remove_hook(const char* path) {
    if (!strstr(path, ".wav") || strstr(path, ".wav.tmp")) return;
#if USE_SD_STORAGE && HAS_AUTO_SHUTDOWN
    assert(!ota_activity_try_begin());
    assert(!ota_activity_try_begin_network_transition());
#endif
}
}

bool storage_mount() { return true; }
void fs_health_get(FSHealthStats* out) { *out = {}; }
bool time_service_ready() { return false; }
#if !(USE_SD_STORAGE && HAS_AUTO_SHUTDOWN)
bool ota_activity_is_busy() { return false; }
#endif
uint32_t esp_random() { static uint32_t value = 0; return ++value; }
bool action_continuation_complete(uint32_t token, bool success) {
    if (expired) return false;
    std::lock_guard<std::mutex> lock(completion_mutex);
    completions[token] = success;
    completion_cv.notify_all();
    return true;
}
bool rtos_create_task_internal_stack(TaskFunction_t entry, const char* name, uint32_t stack,
                                     void* arg, UBaseType_t priority, TaskHandle_t* task,
                                     RtosTaskInternalAlloc*) {
    if (strcmp(name, "VoiceCapture") == 0) capture_internal_stack = true;
    return xTaskCreatePinnedToCore(entry, name, stack, arg, priority, task, 0) == pdPASS;
}
bool rtos_create_task_psram_stack(TaskFunction_t entry, const char* name, uint32_t stack,
                                  void* arg, UBaseType_t priority, TaskHandle_t* task,
                                  RtosTaskPsramAlloc*) {
    if (no_psram) return false;
    capture_psram_stack = true;
    return xTaskCreatePinnedToCore(entry, name, stack, arg, priority, task, 0) == pdPASS;
}
AudioInputFormat audio_input_format() { return {16000, 1, 16}; }
bool audio_input_capture_owned() { return owned; }
bool audio_input_start_capture() {
    start_entered = true;
    while (block_start) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    owned = true;
    return true;
}
bool audio_input_pause_capture() { ++pauses; return owned; }
bool audio_input_stop_capture() {
    if (block_stop) {
        stop_entered = true;
        while (block_stop) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (fail_stop) return false;
    owned = false;
    return true;
}
AudioInputReadStatus audio_input_capture_status() { return AudioInputReadStatus::Ok; }
AudioInputReadResult audio_input_read_frames(int16_t* samples, size_t count, uint32_t) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (audio_test_stopping) throw AudioTestStop{};
    read_entered = true;
    while (block_read) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (force_overrun) return {0, AudioInputReadStatus::Overrun};
    if (no_audio) return {0, AudioInputReadStatus::Timeout};
    ++reads;
    for (size_t i = 0; i < count; ++i) samples[i] = static_cast<int16_t>(i);
    return {count, AudioInputReadStatus::Ok};
}

static bool wait_completion(uint32_t token) {
    std::unique_lock<std::mutex> lock(completion_mutex);
    assert(completion_cv.wait_for(lock, std::chrono::seconds(3),
                                  [=] { return completions.count(token); }));
    return completions[token];
}

template <typename Predicate>
static void wait_until(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!predicate() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(predicate());
}

static VoiceClipSnapshot snapshot() {
    VoiceClipSnapshot value{};
    voice_clips_snapshot(&value);
    return value;
}

int main(int argc, char**) {
    no_psram = argc > 1;
    LittleFS.usage_hook = usage_hook;
    LittleFS.write_hook = write_hook;
    assert(voice_clips_init());
    assert(capture_psram_stack == !no_psram && capture_internal_stack == no_psram);
    assert(voice_clips_command(VoiceClipCommand::Start, 1));
    assert(wait_completion(1));
    wait_until([] { return reads.load() > 0; });
    assert(voice_clips_command(VoiceClipCommand::Pause, 2));
    assert(wait_completion(2));
    assert(snapshot().state == VoiceClipState::Paused && owned);
    const uint64_t paused_duration = snapshot().duration_ms;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(snapshot().duration_ms == paused_duration);
    assert(voice_clips_command(VoiceClipCommand::Resume, 3));
    assert(wait_completion(3));
    assert(voice_clips_command(VoiceClipCommand::Save, 4));
    assert(wait_completion(4));
    VoiceClipSnapshot saved = snapshot();
    assert(saved.state == VoiceClipState::Idle && !owned && saved.last_clip[0]);
    assert(voice_clips_exists(saved.last_clip));
    const String path = voice_clips_path(saved.last_clip);
    const auto& file = LittleFS.files.at(path.c_str());
    VoiceWavInfo info{};
    assert(voice_wav_read_header(file.data(), file.size(), file.size(), &info));
    assert(info.sample_rate == 16000 && info.channels == 1 && info.data_bytes > 0);
    assert(info.data_bytes % 2048 == 0);
    assert(saved.duration_ms == static_cast<uint64_t>(info.data_bytes / 2) * 1000 / 16000);
    for (size_t offset = VOICE_WAV_HEADER_BYTES; offset < file.size(); offset += 2) {
        const unsigned sample = file[offset] | (unsigned(file[offset + 1]) << 8);
        assert(sample == ((offset - VOICE_WAV_HEADER_BYTES) / 2) % 1024);
    }
    File readers[4], other;
    for (auto& reader : readers)
        assert(voice_clips_open(saved.last_clip, &reader) == VoiceClipFileStatus::Ready);
    assert(voice_clips_open(saved.last_clip, &other) == VoiceClipFileStatus::Busy);
    assert(voice_clips_remove(saved.last_clip) == VoiceClipFileStatus::Busy);
    for (auto& reader : readers) voice_clips_close(&reader, saved.last_clip);
    assert(voice_clips_open("missing", &other) == VoiceClipFileStatus::NotFound);
    assert(!storage_browser_path_is_safe("/storage/./voice-clips/file.wav.tmp"));
    assert(!storage_browser_path_is_safe("/./storage/voice-clips"));
    assert(!storage_browser_path_is_safe("/storage/."));
    assert(storage_browser_path_is_safe("/storage/other"));
    const char* storage_error = nullptr;
    JsonDocument listing;
    assert(storage_browser_list("/storage/voice-clips", listing.to<JsonObject>(), storage_error));
    bool found_clip = false;
    for (JsonObject item : listing["entries"].as<JsonArray>())
        found_clip |= item["path"].as<String>() == path.c_str();
    assert(found_clip);
    const String extra_path = "/storage/voice-clips/extra.wav";
    LittleFS.files[extra_path.c_str()] = {1, 2, 3};
    assert(storage_browser_remove(extra_path, storage_error));
    assert(!LittleFS.exists(extra_path.c_str()));
    assert(voice_clips_exists(saved.last_clip));

    block_start = true;
    start_entered = false;
    assert(voice_clips_command(VoiceClipCommand::Start, 5));
    wait_until([] { return start_entered.load(); });
    assert(voice_clips_cancel() == VoiceClipCancelStatus::Pending);
    assert(!voice_clips_command(VoiceClipCommand::Pause, 6));
    block_start = false;
    assert(!wait_completion(5));
    wait_until([] { return voice_clips_cancel() == VoiceClipCancelStatus::Ready; });
    assert(snapshot().state == VoiceClipState::Idle && !owned);
    assert(voice_clips_exists(saved.last_clip));
    for (const auto& entry : LittleFS.files)
        assert(entry.first.find(".wav.tmp") == std::string::npos);

    expired = true;
    assert(voice_clips_command(VoiceClipCommand::Start, 7));
    wait_until([] { return snapshot().state == VoiceClipState::Error && !snapshot().session_active; });
    expired = false;
    assert(voice_clips_command(VoiceClipCommand::Start, 8));
    assert(wait_completion(8));
    force_overrun = true;
    wait_until([] { return snapshot().state == VoiceClipState::Error && !snapshot().session_active; });
    force_overrun = false;
    assert(voice_clips_exists(saved.last_clip));

    no_audio = true;
    assert(voice_clips_command(VoiceClipCommand::Start, 9));
    assert(wait_completion(9));
    assert(voice_clips_command(VoiceClipCommand::Save, 10));
    assert(!wait_completion(10));
    assert(snapshot().state == VoiceClipState::Error && !owned);
    assert(strcmp(snapshot().error, "No audio captured") == 0);
    assert(!snapshot().last_clip[0]);
    no_audio = false;

    assert(voice_clips_command(VoiceClipCommand::Start, 11));
    assert(wait_completion(11));
    const unsigned prior_reads = reads;
    wait_until([=] { return reads.load() > prior_reads; });
    assert(voice_clips_command(VoiceClipCommand::Pause, 12));
    assert(wait_completion(12));
    LittleFS.short_write = true;
    assert(voice_clips_command(VoiceClipCommand::Save, 13));
    assert(!wait_completion(13));
    LittleFS.short_write = false;
    assert(snapshot().state == VoiceClipState::Error && !owned && !snapshot().last_clip[0]);
    assert(voice_clips_exists(saved.last_clip));

    assert(voice_clips_command(VoiceClipCommand::Start, 14));
    assert(wait_completion(14));
    assert(voice_clips_command(VoiceClipCommand::Discard, 15));
    assert(wait_completion(15));
    assert(snapshot().state == VoiceClipState::Idle && !owned && !snapshot().last_clip[0]);
    assert(voice_clips_exists(saved.last_clip));

    block_read = true;
    read_entered = false;
    assert(voice_clips_command(VoiceClipCommand::Start, 16));
    assert(wait_completion(16));
    wait_until([] { return read_entered.load(); });
    assert(voice_clips_command(VoiceClipCommand::Pause, 17));
    force_overrun = true;
    block_read = false;
    assert(!wait_completion(17));
    force_overrun = false;
    assert(snapshot().state == VoiceClipState::Error && !owned);

    no_audio = true;
    assert(voice_clips_command(VoiceClipCommand::Start, 18));
    assert(wait_completion(18));
    wait_until([] { return snapshot().state == VoiceClipState::Error && !snapshot().session_active; });
    assert(strcmp(snapshot().error, "Microphone capture stalled") == 0);
    no_audio = false;

    LittleFS.capacity = 65536;
    assert(voice_clips_command(VoiceClipCommand::Start, 19));
    assert(!wait_completion(19));
    assert(strcmp(snapshot().error, "Insufficient recording storage") == 0);
    LittleFS.capacity = 1024 * 1024;
    assert(voice_clips_exists(saved.last_clip));

    assert(voice_clips_command(VoiceClipCommand::Start, 20));
    assert(wait_completion(20));
    const unsigned reads_before_save = reads;
    wait_until([=] { return reads.load() > reads_before_save; });
    assert(voice_clips_command(VoiceClipCommand::Pause, 21));
    assert(wait_completion(21));
    block_publication = true;
    publication_entered = false;
    LittleFS.rename_hook = publication_hook;
    LittleFS.remove_hook = publication_remove_hook;
    assert(voice_clips_command(VoiceClipCommand::Save, 22));
    wait_until([] { return publication_entered.load(); });
    assert(voice_clips_cancel() == VoiceClipCancelStatus::Pending);
    block_publication = false;
    assert(!wait_completion(22));
    LittleFS.rename_hook = nullptr;
    LittleFS.remove_hook = nullptr;
    assert(voice_clips_cancel() == VoiceClipCancelStatus::Ready);
#if USE_SD_STORAGE && HAS_AUTO_SHUTDOWN
    assert(ota_activity_try_begin());
    ota_activity_finish();
#endif
    assert(snapshot().state == VoiceClipState::Idle && !snapshot().last_clip[0]);
    assert(voice_clips_exists(saved.last_clip));
    for (const auto& entry : LittleFS.files)
        assert(entry.first.find(".wav.tmp") == std::string::npos);

    LittleFS.capacity = 10 * 1024 * 1024;
    const unsigned before_buffered = reads;
    assert(voice_clips_command(VoiceClipCommand::Start, 23));
    assert(wait_completion(23));
    capacity_entered = false;
    block_capacity = true;
    wait_until([] { return capacity_entered.load(); });
    const unsigned while_blocked = reads;
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    assert(reads.load() > while_blocked);
    block_capacity = false;
    assert(voice_clips_command(VoiceClipCommand::Pause, 24));
    assert(wait_completion(24));
    const unsigned buffered_frames = reads - before_buffered;
    assert(voice_clips_command(VoiceClipCommand::Save, 25));
    assert(wait_completion(25));
    const String buffered_path = voice_clips_path(snapshot().last_clip);
    const auto& buffered_file = LittleFS.files.at(buffered_path.c_str());
    assert(voice_wav_read_header(buffered_file.data(), buffered_file.size(), buffered_file.size(), &info));
    assert(info.data_bytes == buffered_frames * 2048);

    block_write = true;
    write_entered = false;
    assert(voice_clips_command(VoiceClipCommand::Start, 26));
    assert(wait_completion(26));
    wait_until([] { return write_entered.load(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    block_write = false;
    wait_until([] { return snapshot().state == VoiceClipState::Error && !snapshot().session_active; });
    assert(!snapshot().last_clip[0] && strcmp(snapshot().error, "Recording buffer overrun") == 0);

    block_write = true;
    write_entered = false;
    assert(voice_clips_command(VoiceClipCommand::Start, 27));
    assert(wait_completion(27));
    wait_until([] { return write_entered.load(); });
    assert(voice_clips_cancel() == VoiceClipCancelStatus::Pending);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(voice_clips_cancel() == VoiceClipCancelStatus::Pending);
    block_write = false;
    wait_until([] { return voice_clips_cancel() == VoiceClipCancelStatus::Ready; });
    assert(!snapshot().last_clip[0] && !owned);

    assert(voice_clips_command(VoiceClipCommand::Start, 28));
    assert(wait_completion(28));
    fail_stop = true;
    assert(voice_clips_command(VoiceClipCommand::Discard, 29));
    assert(!wait_completion(29));
    assert(snapshot().session_active && owned);
    assert(voice_clips_command(VoiceClipCommand::Start, 40));
    assert(!wait_completion(40));
    assert(snapshot().session_active && owned);
    fail_stop = false;
    assert(voice_clips_command(VoiceClipCommand::Start, 41));
    assert(wait_completion(41));
    assert(snapshot().state == VoiceClipState::Recording && owned);
    assert(voice_clips_command(VoiceClipCommand::Discard, 42));
    assert(wait_completion(42));
    assert(!snapshot().session_active && !owned);

    assert(voice_clips_command(VoiceClipCommand::Start, 43));
    assert(wait_completion(43));
    fail_stop = true;
    assert(voice_clips_command(VoiceClipCommand::Discard, 44));
    assert(!wait_completion(44));
    fail_stop = false;
    stop_entered = false;
    block_stop = true;
    assert(voice_clips_command(VoiceClipCommand::Start, 45));
    wait_until([] { return stop_entered.load(); });
    assert(voice_clips_cancel() == VoiceClipCancelStatus::Pending);
    block_stop = false;
    assert(!wait_completion(45));
    wait_until([] { return voice_clips_cancel() == VoiceClipCancelStatus::Ready; });
    assert(!snapshot().session_active && !owned);

    assert(voice_clips_command(VoiceClipCommand::Start, 30));
    assert(wait_completion(30));
    const unsigned before_limit = reads;
    wait_until([=] { return reads.load() > before_limit; });
    block_read = true;
    read_entered = false;
    wait_until([] { return read_entered.load(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    LittleFS.capacity = LittleFS.usedBytes() + 65536;
    assert(voice_clips_command(VoiceClipCommand::Pause, 31));
    block_read = false;
    assert(wait_completion(31));
    assert(snapshot().state == VoiceClipState::Idle && snapshot().last_clip[0]);
    assert(voice_clips_exists(snapshot().last_clip));

    audio_test_stopping = true;
    xTaskNotifyGive(&audio_test_worker_handle);
    xTaskNotifyGive(&audio_test_worker2_handle);
    audio_test_worker.join();
    audio_test_worker2.join();
}
