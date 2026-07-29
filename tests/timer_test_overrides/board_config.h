#pragma once

#define BOARD_CONFIG_H
#define HAS_DISPLAY 1
#define HAS_BUTTON 0
#define HAS_MQTT 0
#define HAS_MCP 1
#define HAS_BLE_HID 0
#define HAS_AUDIO 0
#define HAS_SOUND_PLAYER 0
#define HAS_VISUAL_ALERT 0
#define HAS_PSRAM 0
#define IS_SHUTTER_TESTER 0
#define UI_SCALE_TIER 0
#define MAX_PADS 16
#define MAX_SCREENS 32
#define SCREEN_HISTORY_MAX 8
#define MIN_USER_BRIGHTNESS 1
#define USE_SD_STORAGE 0

#include <stddef.h>
#include <string.h>
#if !defined(__GLIBC__) || (__GLIBC__ < 2) || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 38)
extern "C" size_t strlcpy(char* dst, const char* src, size_t siz);
#endif
