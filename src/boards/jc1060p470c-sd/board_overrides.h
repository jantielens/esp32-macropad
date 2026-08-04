#ifndef BOARD_OVERRIDES_JC1060P470C_SD_H
#define BOARD_OVERRIDES_JC1060P470C_SD_H

#include "../jc1060p470c/board_overrides.h"

// SDMMC slot 0 is provided by the ESP32-P4 FQBN on GPIO39-44. The shared
// FQBN also reserves GPIO45 for its active-low startup pulse; it is not an
// application-controlled card-power GPIO on this board.
#define HAS_SD_CARD true
#define USE_SD_STORAGE true
#define SDMMC_BUS_WIDTH 4
#define SDMMC_MAX_FREQUENCY_KHZ 20000
#define SDMMC_LDO_CHANNEL 4

#endif // BOARD_OVERRIDES_JC1060P470C_SD_H