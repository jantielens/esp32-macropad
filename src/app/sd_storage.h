#pragma once

#include "board_config.h"

#if USE_SD_STORAGE

// Mount the SD card and make it available via the `Storage` macro
// (see storage.h). Only declared when `USE_SD_STORAGE` is true; callers must
// guard usage with the same flag.
//
// Returns true on success. Caller should halt boot on failure — the device
// has no fallback storage when SD is selected as the primary backend.
//
// Expected boot sequence (in app.ino setup()):
//   1. Initialize display (so a halt-screen is visible).
//   2. Call sd_storage_mount().
//   3. On false: show "SD CARD MISSING" on splash and busy-loop.
bool sd_storage_mount();

#endif
