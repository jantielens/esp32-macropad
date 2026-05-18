#include "sd_storage.h"

#if USE_SD_STORAGE

#include "log_manager.h"

#include <Arduino.h>
#include <SD_MMC.h>

#define TAG "SDStor"

// ----------------------------------------------------------------------------
// Pin mapping (hardcoded for jc4880p433 — the only board enabling
// USE_SD_STORAGE today). Future boards should externalize these to
// board_overrides.h #defines and reference them here.
// ----------------------------------------------------------------------------
// SDMMC Slot 0 IOMUX pins on ESP32-P4
static constexpr int SD_PIN_CLK   = 43;
static constexpr int SD_PIN_CMD   = 44;
static constexpr int SD_PIN_D0    = 39;
static constexpr int SD_PIN_D1    = 40;
static constexpr int SD_PIN_D2    = 41;
static constexpr int SD_PIN_D3    = 42;
static constexpr int SD_PIN_POWER = 45;  // Active LOW power enable
static constexpr int SD_LDO_CHANNEL = 4;

bool sd_storage_mount() {
    LOGI(TAG, "Powering SD card (GPIO%d LOW + LDO ch%d)", SD_PIN_POWER, SD_LDO_CHANNEL);
    pinMode(SD_PIN_POWER, OUTPUT);
    digitalWrite(SD_PIN_POWER, LOW);
    delay(10);

    if (!SD_MMC.setPins(SD_PIN_CLK, SD_PIN_CMD,
                        SD_PIN_D0, SD_PIN_D1, SD_PIN_D2, SD_PIN_D3)) {
        LOGE(TAG, "SD_MMC.setPins() failed");
        return false;
    }

    SD_MMC.setPowerChannel(SD_LDO_CHANNEL);

    // begin(mountpoint, mode1bit, format_if_mount_failed)
    // mode1bit = false  → request 4-bit bus
    // format_if_mount_failed = false → never auto-format a misconfigured card
    if (!SD_MMC.begin("/sdcard", false, false)) {
        LOGE(TAG, "SD_MMC.begin() failed — card missing, unformatted, or wiring fault");
        return false;
    }

    const uint8_t type = SD_MMC.cardType();
    const char* type_name =
        (type == CARD_NONE) ? "NONE" :
        (type == CARD_MMC)  ? "MMC"  :
        (type == CARD_SD)   ? "SDSC" :
        (type == CARD_SDHC) ? "SDHC" : "UNKNOWN";
    LOGI(TAG, "SD card mounted: type=%s size=%llu MB",
         type_name, SD_MMC.cardSize() / (1024ULL * 1024ULL));
    LOGI(TAG, "FS used=%llu MB total=%llu MB",
         SD_MMC.usedBytes() / (1024ULL * 1024ULL),
         SD_MMC.totalBytes() / (1024ULL * 1024ULL));
    return true;
}

#endif  // USE_SD_STORAGE
