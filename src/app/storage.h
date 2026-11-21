#ifndef STORAGE_H
#define STORAGE_H

#pragma once

// ============================================================================
// Storage Facade — compile-time selection of the persistent filesystem backend
// ============================================================================
//
// Provides a `Storage` macro that resolves to the Arduino `LittleFS`
// global (internal flash), `SD_MMC` (SDMMC card), or `SD` (SPI card).
// All implement the same
// `fs::FS` API surface, so call sites can use `Storage.open()`,
// `Storage.exists()`, `Storage.usedBytes()`, etc. without caring which
// backend is active.
//
// HAS_FILESYSTEM=false excludes all backends; no Storage object is available.
// The backend selection is driven by the `USE_SD_STORAGE` compile-time flag from
// `board_config.h`. When `USE_SD_STORAGE` is false (the default), this
// header is a no-op rename of `LittleFS` and behavior is identical to the
// pre-facade code.
//
// Pair this with `sd_storage_mount()` (see sd_storage.h) which must be
// called at boot before any `Storage.*` access when `USE_SD_STORAGE` is on.

#include "board_config.h"
#include <stddef.h>
#include <stdint.h>

#if (USE_SD_STORAGE && HAS_AUTO_SHUTDOWN) || defined(ARDUINO_ARCH_ESP32)
  #include "log_manager.h"
  #include "ota_activity.h"
#endif

#if HAS_FILESYSTEM && USE_SD_STORAGE && defined(SD_STORAGE_SPI) && SD_STORAGE_SPI
  #include <SD.h>
  #define Storage SD
#elif HAS_FILESYSTEM && USE_SD_STORAGE
  #include <SD_MMC.h>
  #define Storage SD_MMC
#elif HAS_FILESYSTEM
  #include <LittleFS.h>
  #define Storage LittleFS
#endif

inline bool storage_mutation_begin() {
#if (USE_SD_STORAGE && HAS_AUTO_SHUTDOWN) || defined(ARDUINO_ARCH_ESP32)
  if (!ota_activity_try_begin_config_write()) {
    LOGW("Storage", "Write rejected: shutdown, update, or network transition is active");
    return false;
  }
#endif
  return true;
}

inline void storage_mutation_finish() {
#if (USE_SD_STORAGE && HAS_AUTO_SHUTDOWN) || defined(ARDUINO_ARCH_ESP32)
  ota_activity_finish_config_write();
#endif
}

class StorageMutationGuard {
public:
  StorageMutationGuard() : admitted_(storage_mutation_begin()) {}
  ~StorageMutationGuard() { if (admitted_) storage_mutation_finish(); }
  StorageMutationGuard(const StorageMutationGuard&) = delete;
  StorageMutationGuard& operator=(const StorageMutationGuard&) = delete;
  explicit operator bool() const { return admitted_; }
  void release() { admitted_ = false; }

private:
  bool admitted_;
};

// Publish current filesystem usage to `fs_health_set_storage_usage()`.
// Usage queries can be slow on LittleFS and large SD volumes, so this helper
// throttles repeat calls to once per minute.
// Pass `force=true` once after mount to populate the cache immediately.
// Capacity is reused between forced refreshes, avoiding a second LittleFS scan.
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
    "/camera", "/config", "/icons", "/sounds", "/fonts", "/storage", "/prints", "/brews", "/boot-logo",
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

#if HAS_FILESYSTEM
bool storage_write_verified(const char* path, const uint8_t* data, size_t length);
bool storage_write_atomic(const char* path, const uint8_t* data, size_t length);
#endif

#endif // STORAGE_H
