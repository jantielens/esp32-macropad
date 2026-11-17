#ifndef BOARD_OVERRIDES_ESP32_P4_LCD4B_SD_H
#define BOARD_OVERRIDES_ESP32_P4_LCD4B_SD_H

#include "../esp32-p4-lcd4b/board_overrides.h"

// Onboard MicroSD: SDMMC slot 0, GPIO39-44, with GPIO45 and LDO4 for power.
#define HAS_SD_CARD true
#define USE_SD_STORAGE true
#ifndef HAS_VOICE_NOTES
#define HAS_VOICE_NOTES true
#endif
#define SDMMC_BUS_WIDTH 4
#define SDMMC_MAX_FREQUENCY_KHZ 20000
#define SDMMC_POWER_PIN 45
#define SDMMC_POWER_ACTIVE_LOW true
#define SDMMC_POWER_SETTLE_MS 10
#define SDMMC_LDO_CHANNEL 4

#endif // BOARD_OVERRIDES_ESP32_P4_LCD4B_SD_H
