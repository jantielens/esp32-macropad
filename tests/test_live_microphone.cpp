#include "audio_input.h"
#include "rtos_task_utils.h"
#include "web_portal_live_microphone.h"

#include <esp_heap_caps.h>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <thread>

namespace {
std::atomic<bool> ota_active{false};
std::atomic<bool> capture_running{false};
std::atomic<unsigned> capture_stops{0};
std::atomic<bool> busy{false};
std::atomic<bool> produce{false};
std::atomic<bool> allocation_fails{false};
std::atomic<bool> task_creation_fails{false};
std::atomic<unsigned> allocations{0};
std::atomic<unsigned> frees{0};
std::atomic<size_t> allocation_size{0};
std::atomic<uint32_t> sample_rate{16000};
std::atomic<uint8_t> channels{1};

template<typename Predicate>
bool wait_until(Predicate predicate) {
    for (unsigned i = 0; i < 500; ++i) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}
}

bool web_portal_is_recovery_ap_active() { return false; }
bool ota_activity_is_active() { return ota_active; }
AudioInputFormat audio_input_format() { return {sample_rate, channels, 16}; }
void* heap_caps_malloc(size_t size, uint32_t caps) {
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    ++allocations;
    allocation_size = size;
    return allocation_fails ? nullptr : malloc(size);
}
void heap_caps_free(void* ptr) { ++frees; free(ptr); }
bool audio_input_start_capture() {
    if (busy) return false;
    capture_running = true;
    return true;
}
bool audio_input_capture_owned() { return capture_running; }
bool audio_input_stop_capture() {
    capture_running = false;
    ++capture_stops;
    return true;
}
AudioInputReadResult audio_input_read_frames(int16_t* frames, size_t count, uint32_t) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (!produce) return {0, AudioInputReadStatus::Timeout};
    for (size_t i = 0; i < count * channels; ++i) frames[i] = 123;
    return {count, AudioInputReadStatus::Ok};
}
bool rtos_create_task_psram_stack(TaskFunction_t entry, const char* name, uint32_t size,
                                  void* arg, UBaseType_t priority, TaskHandle_t* task,
                                  RtosTaskPsramAlloc*) {
    if (task_creation_fails) return false;
    return xTaskCreatePinnedToCore(entry, name, size, arg, priority, task, 0) == pdPASS;
}

int main() {
    allocation_fails = true;
    AsyncWebServerRequest no_psram;
    handleLiveMicrophoneStream(&no_psram);
    assert(no_psram.code == 503 && allocations == 1 && frees == 0);

    allocation_fails = false;
    task_creation_fails = true;
    AsyncWebServerRequest no_task;
    handleLiveMicrophoneStream(&no_task);
    assert(no_task.code == 503 && allocations == 2 && frees == 1);
    task_creation_fails = false;

    AsyncWebServerRequest request;
    handleLiveMicrophoneStream(&request);
    assert(allocation_size >= 16000 * 2 && allocation_size < 16000 * 2 + 4096);
    assert(wait_until([] { return capture_running.load(); }));
    request.poll();
    assert(request.connection.output.find("LM") != std::string::npos);
    const size_t header_size = request.connection.output.size();
    request.poll();
    request.poll();
    produce = true;
    assert(wait_until([&] { request.poll(); return request.connection.output.size() > header_size; }));

    AsyncWebServerRequest second;
    handleLiveMicrophoneStream(&second);
    assert(second.code == 409 && capture_running);
    AsyncWebServerRequest missing_header;
    missing_header.has_header = false;
    handleLiveMicrophoneStream(&missing_header);
    assert(missing_header.code == 403);

    ota_active = true;
    assert(wait_until([] { return !capture_running.load() && capture_stops.load() == 1; }));
    assert(wait_until([&] { request.poll(); return request.connection.closed; }));
    request.disconnect();
    assert(wait_until([] { return frees.load() == 2; }));

    AsyncWebServerRequest during_ota;
    handleLiveMicrophoneStream(&during_ota);
    assert(during_ota.code == 409);

    ota_active = false;
    sample_rate = 48000;
    channels = 2;
    AsyncWebServerRequest next;
    handleLiveMicrophoneStream(&next);
    assert(allocation_size >= 48000 * 4 && allocation_size < 48000 * 4 + 4096);
    assert(wait_until([] { return capture_running.load(); }));
    next.disconnect();
    assert(wait_until([] { return !capture_running.load() && capture_stops.load() == 2; }));
    assert(wait_until([] { return frees.load() == 3; }));

    audio_test_stopping = true;
    audio_test_cv.notify_all();
    audio_test_worker.join();
}
