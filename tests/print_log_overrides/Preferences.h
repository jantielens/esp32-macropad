// ============================================================================
// Test override: Preferences.h — empty stub for the print_log host test
// ============================================================================
// print_log.cpp does `#include <Preferences.h>`. The print_log test defines
// its own in-memory `Preferences` class, so this stub exists only to satisfy
// the angle-bracket include on the host toolchain and deliberately declares
// nothing to avoid colliding with the test's own definition.
#pragma once
