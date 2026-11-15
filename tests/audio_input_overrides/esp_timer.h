#pragma once
#include <chrono>
#include <atomic>
#include <stdint.h>

inline std::atomic<int64_t> audio_test_time_offset_us{0};
inline int64_t esp_timer_get_time() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count() + audio_test_time_offset_us.load();
}
