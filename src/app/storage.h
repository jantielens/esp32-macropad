#pragma once

// ============================================================================
// Storage Facade — compile-time selection of the persistent filesystem backend
// ============================================================================
//
// Provides a `Storage` macro that resolves to either the Arduino `LittleFS`
// global (internal-flash backend, the default for every board) or the
// `SD_MMC` global (external SD card backend). Both implement the same
// `fs::FS` API surface, so call sites can use `Storage.open()`,
// `Storage.exists()`, `Storage.usedBytes()`, etc. without caring which
// backend is active.
//
// The selection is driven by the `USE_SD_STORAGE` compile-time flag from
// `board_config.h`. When `USE_SD_STORAGE` is false (the default), this
// header is a no-op rename of `LittleFS` and behavior is identical to the
// pre-facade code.
//
// Pair this with `sd_storage_mount()` (see sd_storage.h) which must be
// called at boot before any `Storage.*` access when `USE_SD_STORAGE` is on.

#include "board_config.h"

#if USE_SD_STORAGE
  #include <SD_MMC.h>
  #define Storage SD_MMC
#else
  #include <LittleFS.h>
  #define Storage LittleFS
#endif

// Publish current filesystem usage to `fs_health_set_storage_usage()`.
// `usedBytes()` / `totalBytes()` walk the FAT on SD cards and can be slow
// on large volumes, so this helper throttles repeat calls to once per minute.
// Pass `force=true` once after mount to populate the cache immediately.
void storage_publish_usage(bool force);
