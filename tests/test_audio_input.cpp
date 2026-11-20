#include "audio_input.h"
#include "rtos_task_utils.h"
#include <esp_heap_caps.h>
#include <freertos/task.h>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <thread>

namespace {
std::atomic<bool> ota_active{false};
std::atomic<bool> stop_fails{false};
std::atomic<bool> start_fails{false};
std::atomic<bool> running{false};
std::atomic<unsigned> starts{0};
std::atomic<unsigned> stops{0};
std::atomic<AudioInputReadStatus> read_status{AudioInputReadStatus::Ok};
std::atomic<int16_t> sample_value{100};
bool no_psram = false;
bool meter_psram_buffer = false;
bool meter_internal_buffer = false;
bool meter_psram_stack = false;
bool meter_internal_stack = false;

class TestInput final : public AudioInputDriver {
public:
    bool inputAvailable() const override { return true; }
    bool captureStart() override {
        running = true;
        ++starts;
        return !start_fails;
    }
    AudioInputReadResult readFrames(int16_t* frames, size_t count, uint32_t) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        const auto status = read_status.load();
        if (status != AudioInputReadStatus::Ok && status != AudioInputReadStatus::Timeout) return {0, status};
        const size_t n = count < 4 ? count : 4;
        for (size_t i = 0; i < n; ++i) frames[i] = sample_value;
        return {n, status};
    }
    AudioInputReadStatus captureStatus() override { return read_status; }
    bool captureStop() override {
        ++stops;
        if (stop_fails) return false;
        running = false;
        return true;
    }
    AudioInputFormat inputFormat() const override { return {16000, 1, 16}; }
};

template<typename Predicate>
bool wait_until(Predicate predicate) {
    for (unsigned i = 0; i < 200; ++i) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

bool another_task_can_start() {
    bool started = false;
    std::thread other([&] {
        int task;
        audio_test_current_task = &task;
        started = audio_input_start_capture();
        if (started) assert(audio_input_stop_capture());
    });
    other.join();
    return started;
}

void test_processing() {
    AudioInputProcessing dsp;
    dsp.configure({0, false}, 16000, 2);
    int16_t neutral[] = {-32768, 32767, 123, -123};
    dsp.process(neutral, 2);
    assert(neutral[0] == -32768 && neutral[1] == 32767 && neutral[2] == 123 && neutral[3] == -123);

    dsp.configure({24, false}, 16000, 2);
    int16_t amplified[] = {-3000, 3000};
    dsp.process(amplified, 1);
    assert(amplified[0] == -32768 && amplified[1] == 32767);
    dsp.configure({-6, false}, 16000, 2);
    int16_t attenuated[] = {10000, -10000};
    dsp.process(attenuated, 1);
    assert(attenuated[0] >= 5011 && attenuated[0] <= 5013);
    assert(attenuated[1] == -attenuated[0]);

    dsp.configure({0, false}, 16000, 2);
    int16_t opposite[] = {-32768, 32767};
    AudioInputBlockDiagnostics diagnostics = {};
    dsp.process(opposite, 1, &diagnostics);
    assert(diagnostics.peak == 32768 && diagnostics.input_full_scale && !diagnostics.gain_clipped);
    assert(diagnostics.sum_squares == 1073741824ULL + 1073676289ULL);

    for (uint32_t rate : {16000U, 48000U}) {
        for (unsigned frequency : {20U, 1000U}) {
            dsp.configure({0, true}, rate, 2);
            dsp.reset();
            double input_energy = 0;
            double output_energy = 0;
            for (uint32_t i = 0; i < rate; ++i) {
                const int16_t input = static_cast<int16_t>(10000.0 * sin(6.283185307179586 * frequency * i / rate));
                int16_t samples[] = {input, 0};
                dsp.process(samples, 1);
                assert(samples[1] == 0);
                if (i >= rate / 2) {
                    input_energy += static_cast<double>(input) * input;
                    output_energy += static_cast<double>(samples[0]) * samples[0];
                }
            }
            const double db = 10.0 * log10(output_energy / input_energy);
            if (frequency == 20) assert(db <= -10.0);
            else assert(db > -1.0);
        }
    }

    AudioInputProcessing whole;
    AudioInputProcessing blocks;
    whole.configure({6, true}, 16000, 2);
    blocks.configure({6, true}, 16000, 2);
    int16_t a[] = {100, -100, 200, -200, 300, -300, 400, -400};
    int16_t b[] = {100, -100, 200, -200, 300, -300, 400, -400};
    whole.process(a, 4);
    blocks.process(b, 1);
    blocks.process(b + 2, 3);
    for (unsigned i = 0; i < 8; ++i) assert(a[i] == b[i]);
    blocks.reset();
    int16_t first[] = {100, -100};
    blocks.process(first, 1);
    assert(first[0] == a[0] && first[1] == a[1]);
    blocks.configure({6, false}, 16000, 2);
    blocks.configure({6, true}, 16000, 2);
    int16_t toggled[] = {100, -100};
    blocks.process(toggled, 1);
    assert(toggled[0] == a[0] && toggled[1] == a[1]);
}
}

void* heap_caps_malloc(size_t size, uint32_t caps) {
    if (caps & MALLOC_CAP_SPIRAM) {
        if (no_psram) return nullptr;
        meter_psram_buffer = true;
    } else if (caps & MALLOC_CAP_INTERNAL) {
        meter_internal_buffer = true;
    }
    return malloc(size);
}
void heap_caps_free(void* ptr) { free(ptr); }
bool psramFound() { return !no_psram; }
size_t heap_caps_get_total_size(uint32_t) { return no_psram ? 0 : 1024 * 1024; }
bool rtos_create_task_psram_stack_pinned(TaskFunction_t entry, const char* name, uint32_t stack,
                                         void* arg, UBaseType_t priority, TaskHandle_t* task,
                                         RtosTaskPsramAlloc*, BaseType_t core) {
    if (no_psram) return false;
    meter_psram_stack = true;
    return xTaskCreatePinnedToCore(entry, name, stack, arg, priority, task, core) == pdPASS;
}
bool rtos_create_task_internal_stack_pinned(TaskFunction_t entry, const char* name, uint32_t stack,
                                            void* arg, UBaseType_t priority, TaskHandle_t* task,
                                            RtosTaskInternalAlloc*, BaseType_t core) {
    meter_internal_stack = true;
    return xTaskCreatePinnedToCore(entry, name, stack, arg, priority, task, core) == pdPASS;
}
bool ota_activity_is_active() { return ota_active; }
AudioInputDriver* audio_input_driver_create() {
    static TestInput driver;
    return &driver;
}

int main(int argc, char**) {
    test_processing();
    no_psram = argc > 1;
    int main_task;
    audio_test_current_task = &main_task;
    assert(audio_input_start_capture());
    int16_t processed[4] = {};
    assert(audio_input_read_frames(processed, 4, 10).frames == 4 && processed[0] == 100);
    assert(audio_input_set_processing({6, false}));
    assert(audio_input_get_processing().gain_db == 6);
    assert(!audio_input_set_processing({25, true}));
    assert(audio_input_get_processing().gain_db == 6 && !audio_input_get_processing().reduce_rumble);
    read_status = AudioInputReadStatus::Timeout;
    assert(audio_input_read_frames(processed, 4, 10).frames == 4 && processed[0] == 200);
    assert(audio_input_set_processing({0, false}));
    read_status = AudioInputReadStatus::Overrun;
    processed[0] = 1234;
    assert(audio_input_read_frames(processed, 4, 10).status == AudioInputReadStatus::Overrun);
    assert(processed[0] == 1234);
    read_status = AudioInputReadStatus::Ok;
    assert(audio_input_stop_capture());
    audio_input_meter_init();
    assert(meter_psram_buffer == !no_psram && meter_internal_buffer == no_psram);
    assert(meter_psram_stack == !no_psram && meter_internal_stack == no_psram);

    std::atomic<bool> request_meter{true};
    std::thread requester([&] {
        while (request_meter) {
            audio_input_meter_request();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });
    assert(wait_until([] { return starts.load() > 0 && running.load(); }));
    assert(wait_until([] {
        AudioInputMeterSnapshot snapshot;
        return audio_input_meter_get_snapshot(&snapshot);
    }));
    assert(audio_input_start_capture());
    AudioInputMeterSnapshot snapshot;
    assert(!audio_input_meter_get_snapshot(&snapshot));
    sample_value = 20000;
    assert(audio_input_set_processing({6, false}));
    audio_input_meter_request();
    assert(audio_input_read_frames(processed, 4, 10).frames == 4);
    auto diagnostics = audio_input_diagnostics();
    assert(diagnostics.fresh && diagnostics.in_use && diagnostics.peak == 32767 && diagnostics.gain_clipped);
    sample_value = 32767;
    assert(audio_input_read_frames(processed, 4, 10).frames == 4);
    assert(audio_input_diagnostics().input_full_scale);
    sample_value = 100;
    assert(audio_input_set_processing({0, false}));
    request_meter = false;
    requester.join();
    assert(audio_input_pause_capture());
    assert(!running && !another_task_can_start());
    assert(audio_input_start_capture() && running);

    read_status = AudioInputReadStatus::Overrun;
    int16_t samples[4];
    assert(audio_input_read_frames(samples, 4, 10).status == AudioInputReadStatus::Overrun);
    assert(audio_input_capture_status() == AudioInputReadStatus::Overrun);
    stop_fails = true;
    assert(!audio_input_stop_capture());
    assert(!audio_input_start_capture());
    assert(!another_task_can_start());
    stop_fails = false;
    assert(audio_input_start_capture() && running);
    assert(audio_input_stop_capture());

    start_fails = true;
    stop_fails = true;
    assert(!audio_input_start_capture() && audio_input_capture_owned());
    stop_fails = false;
    assert(!audio_input_start_capture() && !audio_input_capture_owned());
    start_fails = false;
    assert(audio_input_start_capture() && running);
    assert(audio_input_stop_capture());

    read_status = AudioInputReadStatus::Ok;
    audio_input_meter_request();
    assert(wait_until([] { return running.load(); }));
    ota_active = true;
    assert(wait_until([] { return !running.load(); }));
    ota_active = false;

    start_fails = true;
    stop_fails = true;
    const unsigned prior_stops = stops;
    audio_input_meter_request();
    assert(wait_until([=] { return stops.load() >= prior_stops + 2; }));
    start_fails = false;
    stop_fails = false;
    assert(wait_until([] { return !running.load(); }));
    assert(another_task_can_start());

    audio_test_stopping = true;
    audio_test_cv.notify_one();
    audio_test_worker.join();
}
