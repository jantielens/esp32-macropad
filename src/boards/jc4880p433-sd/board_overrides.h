#ifndef BOARD_OVERRIDES_JC4880P433_SD_H
#define BOARD_OVERRIDES_JC4880P433_SD_H

#include "../jc4880p433/board_overrides.h"

// On-board MicroSD slot: ESP32-P4 SDMMC slot 0, 4-bit at 20 MHz.
// GPIO45 powers the card when driven low; LDO4 supplies the slot.
#define HAS_SD_CARD true
#define USE_SD_STORAGE true
#define SDMMC_BUS_WIDTH 4
#define SDMMC_MAX_FREQUENCY_KHZ 20000
#define SDMMC_POWER_PIN 45
#define SDMMC_POWER_ACTIVE_LOW true
#define SDMMC_POWER_SETTLE_MS 10
#define SDMMC_LDO_CHANNEL 4

#endif // BOARD_OVERRIDES_JC4880P433_SD_H