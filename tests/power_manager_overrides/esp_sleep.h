#pragma once
#include <cstdint>

inline bool test_power_slept = false;
inline uint64_t test_power_sleep_us = 0;
inline void esp_sleep_enable_timer_wakeup(uint64_t duration) { test_power_sleep_us = duration; }
inline void esp_deep_sleep_start() { test_power_slept = true; }
