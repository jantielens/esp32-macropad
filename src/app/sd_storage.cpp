#include "sd_storage.h"

#if USE_SD_STORAGE

#include "fs_health.h"
#include "log_manager.h"

#include <Arduino.h>
#include "storage.h"
#if SD_STORAGE_SPI
#include <SPI.h>
#else
#include <SD_MMC.h>
#endif

#define TAG "SDStor"

static bool s_mounted = false;

static FSCardType fs_card_type_from_sd(uint8_t card_type) {
    if (card_type == CARD_NONE) return FS_CARD_TYPE_NONE;
    if (card_type == CARD_SD) return FS_CARD_TYPE_SD;
    if (card_type == CARD_SDHC) return FS_CARD_TYPE_SDHC;
    return FS_CARD_TYPE_UNKNOWN;
}

bool sd_storage_mount() {
    if (s_mounted) return true;

#if SD_STORAGE_SPI
    if (!SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN)) {
        SPI.end();
        LOGE(TAG, "SPI SD bus initialization failed");
        return false;
    }
    if (!SD.begin(SD_SPI_CS_PIN, SPI, SD_SPI_FREQUENCY_HZ, "/sdcard")) {
        SPI.end();
        LOGE(TAG, "SPI SD mount failed; check the card, filesystem, wiring, and power, then reboot");
        return false;
    }
#else
#if SDMMC_POWER_PIN >= 0
    pinMode(SDMMC_POWER_PIN, OUTPUT);
    digitalWrite(SDMMC_POWER_PIN, SDMMC_POWER_ACTIVE_LOW ? LOW : HIGH);
#if SDMMC_POWER_SETTLE_MS > 0
    delay(SDMMC_POWER_SETTLE_MS);
#endif
#endif

#if SDMMC_LDO_CHANNEL >= 0
    SD_MMC.setPowerChannel(SDMMC_LDO_CHANNEL);
#endif

#if SDMMC_CLK_PIN >= 0
    SD_MMC.setPins(SDMMC_CLK_PIN, SDMMC_CMD_PIN, SDMMC_D0_PIN);
#endif

    if (!SD_MMC.begin("/sdcard", SDMMC_BUS_WIDTH == 1, false,
                      SDMMC_MAX_FREQUENCY_KHZ)) {
        LOGE(TAG, "SDMMC mount failed; check the card, filesystem, wiring, and power, then reboot");
        return false;
    }
#endif

    const uint8_t type = Storage.cardType();
    const FSCardType health_card_type = fs_card_type_from_sd(type);
    if (health_card_type == FS_CARD_TYPE_NONE) {
        LOGE(TAG, "SD mount reported no card; check the card, filesystem, wiring, and power, then reboot");
    #if SD_STORAGE_SPI
        SD.end();
        SPI.end();
    #endif
        return false;
    }

    fs_health_set_sd_card_type(health_card_type);
    s_mounted = true;
    const char* type_name = health_card_type == FS_CARD_TYPE_SD ? "SD" :
                            health_card_type == FS_CARD_TYPE_SDHC ? "SDHC" : "UNKNOWN";
    LOGI(TAG, "SD card mounted: type=%s size=%llu MB",
         type_name, Storage.cardSize() / (1024ULL * 1024ULL));
    LOGI(TAG, "FS used=%llu MB total=%llu MB",
         Storage.usedBytes() / (1024ULL * 1024ULL),
         Storage.totalBytes() / (1024ULL * 1024ULL));
    return true;
}

bool sd_storage_is_mounted() {
    return s_mounted;
}

#endif  // USE_SD_STORAGE
