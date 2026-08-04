#ifndef BOARD_OVERRIDES_JC3636W518_SD_H
#define BOARD_OVERRIDES_JC3636W518_SD_H

#include "../jc3636w518/board_overrides.h"

// On-board TF card slot: SDMMC 1-bit on GPIO2 (D0), GPIO3 (CLK), GPIO4 (CMD).
#define HAS_SD_CARD true
#define USE_SD_STORAGE true
#define SDMMC_BUS_WIDTH 1
#define SDMMC_MAX_FREQUENCY_KHZ 20000
#define SDMMC_CLK_PIN 3
#define SDMMC_CMD_PIN 4
#define SDMMC_D0_PIN 2

#endif // BOARD_OVERRIDES_JC3636W518_SD_H