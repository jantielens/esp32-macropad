#pragma once
#include <cstdint>

inline bool test_power_slept = false;
inline void esp_sleep_enable_timer_wakeup(uint64_t) {}
inline void esp_deep_sleep_start() { test_power_slept = true; }
