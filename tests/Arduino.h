// Test stub: Arduino.h — minimal shim for host-compiled brew_manager tests
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>

// Mock millis() — test code sets this directly
extern uint32_t g_mock_millis;
inline uint32_t millis() { return g_mock_millis; }
