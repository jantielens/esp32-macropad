#pragma once
#include <cstdint>

constexpr int ESP_OK = 0;
constexpr int ESP_TIMER_TASK = 0;
struct esp_timer_create_args_t {
    void (*callback)(void*) = nullptr;
    int dispatch_method = 0;
    const char* name = nullptr;
};
using esp_timer_handle_t = esp_timer_create_args_t*;
inline esp_timer_create_args_t test_power_timer;
inline uint64_t test_power_timer_duration = 0;
inline bool test_power_timer_running = false;
inline int esp_timer_create(const esp_timer_create_args_t* args, esp_timer_handle_t* handle) {
    test_power_timer = *args;
    *handle = &test_power_timer;
    return ESP_OK;
}
inline int esp_timer_start_once(esp_timer_handle_t, uint64_t duration) {
    test_power_timer_duration = duration;
    test_power_timer_running = true;
    return ESP_OK;
}
inline int esp_timer_stop(esp_timer_handle_t) {
    test_power_timer_running = false;
    return ESP_OK;
}
