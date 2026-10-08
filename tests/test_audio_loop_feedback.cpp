#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <vector>

#include "../src/app/audio.h"
#include "../src/app/audio_output_driver.h"
#include "../src/app/loop_feedback.h"

using TickType_t = uint32_t;
using TaskHandle_t = void*;
using TaskFunction_t = void (*)(void*);
using BaseType_t = int;
using UBaseType_t = unsigned;
using portMUX_TYPE = int;
struct RtosTaskInternalAlloc {};
struct WorkerIdle {};
struct TestQueue {
    size_t capacity;
    size_t item_size;
    std::deque<std::vector<uint8_t>> items;
};
using QueueHandle_t = TestQueue*;

#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(value) (value)
#define portMUX_INITIALIZER_UNLOCKED 0

static void portENTER_CRITICAL(portMUX_TYPE* lock) { assert((*lock)++ == 0); }
static void portEXIT_CRITICAL(portMUX_TYPE* lock) { assert(--(*lock) == 0); }
static QueueHandle_t xQueueCreate(size_t capacity, size_t item_size) {
    return new TestQueue{capacity, item_size, {}};
}
static BaseType_t xQueueReceive(QueueHandle_t queue, void* item, TickType_t wait) {
    if (queue->items.empty()) {
        if (wait) throw WorkerIdle{};
        return pdFALSE;
    }
    memcpy(item, queue->items.front().data(), queue->item_size);
    queue->items.pop_front();
    return pdTRUE;
}
static BaseType_t xQueueSend(QueueHandle_t queue, const void* item, TickType_t) {
    if (queue->items.size() == queue->capacity) return pdFALSE;
    const auto* bytes = static_cast<const uint8_t*>(item);
    queue->items.emplace_back(bytes, bytes + queue->item_size);
    return pdTRUE;
}
static void vQueueDelete(QueueHandle_t queue) { delete queue; }
static void vTaskDelay(TickType_t) { throw WorkerIdle{}; }
static bool rtos_create_task_internal_stack_pinned(TaskFunction_t, const char*, uint32_t,
                                                  void*, UBaseType_t, TaskHandle_t*,
                                                  RtosTaskInternalAlloc*, BaseType_t) { return true; }
static void device_telemetry_log_memory_snapshot(const char*) {}
static int64_t esp_timer_get_time() { return 1; }
static bool ota_active = false;
static uint32_t ota_epoch = 0;
bool ota_activity_is_active() { return ota_active; }
uint32_t ota_activity_epoch() { return ota_epoch; }

class TestOutput : public AudioOutputDriver {
public:
    std::vector<int16_t> pcm;
    std::function<void()> on_write;
    uint8_t volume = 0;
    bool fail = false;
    bool begin(uint32_t) override { return true; }
    void setVolume(uint8_t value) override { volume = value; }
    bool write(const int16_t* frames, size_t count) override {
        pcm.insert(pcm.end(), frames, frames + count * 2);
        if (on_write) on_write();
        return !fail;
    }
};
static TestOutput output;
void AudioOutputDriver::setMuted(bool) {}
AudioOutputDriver* audio_output_driver_create() { return &output; }

#include "audio_worker_under_test.inc"

static bool guard_valid = true;
static unsigned failure_count = 0;
static bool playback_guard(uint32_t generation) { return guard_valid && generation == 7; }
static void playback_failure(AudioPlaybackGuard, uint32_t) { ++failure_count; }
static void run_worker() {
    try { audio_task(nullptr); } catch (const WorkerIdle&) {}
}
static void reset() {
    ota_active = false;
    guard_valid = true;
    failure_count = 0;
    audio_stop();
    output.pcm.clear();
    output.on_write = nullptr;
    output.fail = false;
}

int main() {
    audio_feedback("800:40");
    audio_init(70);
    reset();
    assert(audio_submit_tone("400:12 12", 80, true, playback_guard, 7, playback_failure));
    const uint32_t owner = g_loop_feedback.command_id;
    const bool stop_before_feedback = g_stop_requested;
    audio_feedback("900:4");
    audio_feedback("1000:4");
    assert(audio_queue->items.size() == 1 && g_stop_requested == stop_before_feedback);
    assert(g_loop_feedback.command_id == owner && g_loop_feedback.pending);
    unsigned writes = 0;
    output.on_write = [&]() {
        ++writes;
        assert(output.volume == 80);
        if (writes == 2) audio_feedback("1200:12");
        if (writes == 12) audio_stop();
    };
    run_worker();
    assert(writes == 12 && failure_count == 0);
    assert(!g_loop_feedback.protected_loop && !g_loop_feedback.pending);
    assert(output.volume == 70 && !g_feedback_tone.active);
    bool mixed_gap = false;
    for (size_t sample = 576 * 2; sample < 1152 * 2; ++sample) {
        if (output.pcm[sample]) mixed_gap = true;
    }
    assert(mixed_gap);

    reset();
    assert(audio_submit_tone("400:12 12", 0, true, playback_guard, 7, playback_failure));
    audio_feedback("800:40");
    guard_valid = false;
    run_worker();
    assert(output.pcm.empty() && !g_loop_feedback.protected_loop && failure_count == 0);

    reset();
    assert(audio_submit_tone("400:12 12", 0, true, playback_guard, 7, playback_failure));
    audio_feedback("800:40");
    output.on_write = []() { guard_valid = false; };
    run_worker();
    assert(!output.pcm.empty() && !g_loop_feedback.protected_loop && failure_count == 0);

    reset();
    assert(audio_submit_tone("400:12 12", 0, true));
    audio_feedback("800:40");
    ota_active = true;
    ++ota_epoch;
    run_worker();
    assert(output.pcm.empty() && audio_queue->items.empty() && !g_loop_feedback.protected_loop);
    audio_feedback("800:40");
    assert(audio_queue->items.empty());

    reset();
    assert(audio_submit_tone("400:12", 0, true));
    AudioCommand stale_command = {};
    stale_command.ota_epoch = ota_epoch;
    ToneAlertOverlay stale_feedback = {};
    assert(tone_alert_overlay_start(&stale_feedback, "800:40", false, 48000, 0.25f));
    ++ota_epoch;
    assert(!audio_enqueue(&stale_command, &stale_feedback));
    assert(g_loop_feedback.protected_loop && !g_loop_feedback.pending);
    run_worker();
    assert(output.pcm.empty() && !g_loop_feedback.protected_loop);

    reset();
    assert(audio_submit_tone("400:12 12", 0, true));
    audio_feedback("800:40");
    output.on_write = []() { ota_active = true; ++ota_epoch; };
    run_worker();
    assert(!g_loop_feedback.protected_loop && !g_feedback_tone.active);

    reset();
    assert(audio_submit_tone("400:12", 0, true, playback_guard, 7, playback_failure));
    audio_feedback("800:40");
    output.fail = true;
    run_worker();
    assert(failure_count == 1 && !g_loop_feedback.protected_loop);

    reset();
    assert(audio_submit_tone("400:12", 0, true));
    audio_feedback("800:40");
    audio_beep("600:4", 0);
    assert(!g_loop_feedback.protected_loop && !g_loop_feedback.pending);
    run_worker();
    assert(output.pcm.size() == 192 * 2);

    reset();
    assert(audio_submit_tone("400:12", 0, true));
    writes = 0;
    uint32_t replacement_owner = 0;
    output.on_write = [&]() {
        ++writes;
        if (writes == 1) {
            assert(audio_submit_tone("600:12", 0, true));
            replacement_owner = g_loop_feedback.command_id;
            audio_feedback("1000:40");
        } else {
            assert(g_loop_feedback.matches(replacement_owner));
            assert(!g_loop_feedback.pending && g_feedback_tone.active);
            audio_stop();
        }
    };
    run_worker();
    assert(writes == 2 && !g_loop_feedback.protected_loop);

    reset();
    assert(audio_submit_tone("400:12", 0, true));
    audio_feedback("none");
    audio_feedback("");
    audio_feedback("bad");
    assert(g_loop_feedback.protected_loop && !g_loop_feedback.pending);
    audio_stop();
    audio_feedback("600:4");
    run_worker();
    assert(output.pcm.size() == 192 * 2);
    vQueueDelete(audio_queue);
    std::puts("audio loop feedback worker: PASS");
}