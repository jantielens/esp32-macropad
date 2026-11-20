#pragma once

#define HAS_SD_CARD true
#define USE_SD_STORAGE true
#ifndef HAS_VOICE_NOTES
#define HAS_VOICE_NOTES true
#endif
#define SD_STORAGE_SPI true
#define SD_SPI_CS_PIN 8
#define SD_SPI_SCK_PIN 13
#define SD_SPI_MOSI_PIN 14
#define SD_SPI_MISO_PIN 12
#define SD_SPI_FREQUENCY_HZ 1000000
// Share the board-owned SPI instance with the display.
#define BOARD_SD_SPI sticky_shared_spi()
// Power the card and configure its shared bus before mounting.
#define BOARD_SD_PREPARE() sticky_prepare_sd()
// Disable the card without tearing down the display bus after a failed mount.
#define BOARD_SD_MOUNT_FAILED() sticky_sd_mount_failed()
// build.sh exports this to separately compiled NimBLE library sources.
#define BLE_BACKEND 1

#include "../reterminal-sticky-interactive/board_overrides.h"

#undef BOARD_HARDWARE_SOURCE
#undef BOARD_HARDWARE_HEADER
#undef BOARD_SENSOR_SOURCE
#undef BOARD_ACTION_SOURCE
#define BOARD_HARDWARE_SOURCE "../reterminal-sticky-interactive/sticky_hardware.cpp"
#define BOARD_HARDWARE_HEADER "../reterminal-sticky-interactive/sticky_hardware.h"
#define BOARD_SENSOR_SOURCE "../reterminal-sticky-interactive/drivers/sticky_sensors.cpp"
#define BOARD_ACTION_SOURCE "../reterminal-sticky-interactive/sticky_actions.cpp"
