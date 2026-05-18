#pragma once

#include "board_config.h"

// Run an early-boot SD-card diagnostic probe. Gated by `SD_PROBE_ON_BOOT`
// — when the flag is false (default), this is a no-op.
//
// The probe attempts mount with several fallbacks (4-bit, 1-bit, no power
// pin) and logs detailed card info, FS stats, a directory listing, and a
// small write/read round-trip. After the probe completes it unmounts and
// powers the card back down, so the normal `sd_storage_mount()` call later
// in boot starts from a clean state.
//
// Intended use: enable temporarily during new-board bring-up to verify
// pin mapping and electrical wiring before flipping `USE_SD_STORAGE`.
void sd_probe_run();
