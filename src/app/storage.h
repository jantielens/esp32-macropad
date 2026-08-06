#ifndef STORAGE_H
#define STORAGE_H

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
void storage_publish_usage(bool force = false);

inline bool storage_boot_should_halt(bool sd_mount_succeeded) {
#if USE_SD_STORAGE
  return !sd_mount_succeeded;
#else
  (void)sd_mount_succeeded;
  return false;
#endif
}

template <typename RemoveRoot>
inline bool storage_remove_sd_owned_roots(RemoveRoot remove_root) {
  static const char* const owned_roots[] = {
    "/config", "/icons", "/sounds", "/storage", "/prints", "/brews",
  };
  bool ok = true;
  for (const char* root : owned_roots) {
    if (!remove_root(root)) ok = false;
  }
  return ok;
}

// Mount the persistent filesystem backend and ensure base directories exist.
// Idempotent — safe to call from multiple subsystems (pad config, hw button
// config, …); only the first call performs the actual mount. Returns true if
// the filesystem is mounted and ready. For SD storage the card must already
// have been mounted via sd_storage_mount() earlier in boot.
bool storage_mount();

#endif // STORAGE_H
