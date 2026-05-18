#pragma once

#include "board_config.h"

// Mount the SD card and make it available via the `Storage` macro
// (see storage.h). Only does anything when `USE_SD_STORAGE` is true.
//
// Returns true on success. Caller should halt boot on failure — the device
// has no fallback storage when SD is selected as the primary backend.
//
// Expected boot sequence (in app.ino setup()):
//   1. Initialize display (so a halt-screen is visible).
//   2. Call sd_storage_mount().
//   3. On false: show "SD CARD MISSING" on splash and busy-loop.
bool sd_storage_mount();
