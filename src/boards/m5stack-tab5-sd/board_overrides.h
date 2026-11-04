#ifndef BOARD_OVERRIDES_M5STACK_TAB5_SD_H
#define BOARD_OVERRIDES_M5STACK_TAB5_SD_H

#include "../m5stack-tab5/board_overrides.h"

#define HAS_SD_CARD true
#define USE_SD_STORAGE true
#define SD_STORAGE_SPI true
#define SD_SPI_CS_PIN 42
#define SD_SPI_SCK_PIN 43
#define SD_SPI_MOSI_PIN 44
#define SD_SPI_MISO_PIN 39
#define SD_SPI_FREQUENCY_HZ 25000000

#endif