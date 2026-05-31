// ============================================================================
// Test override: LittleFS.h — empty stub for the print_log host test
// ============================================================================
// src/app/storage.h does `#include <LittleFS.h>` then `#define Storage
// LittleFS`. The print_log test (tests/test_print_log.cpp) already defines its
// own in-memory `FakeLittleFS` class plus a global `LittleFS` instance, so the
// Storage macro resolves to that mock. This stub exists only so the angle-
// bracket include resolves on the host toolchain (the real ESP32 LittleFS.h is
// not on the host include path). It deliberately declares nothing to avoid
// colliding with the test's own File/LittleFS definitions.
#pragma once
