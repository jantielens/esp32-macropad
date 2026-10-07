#ifndef BOARD_OVERRIDES_ESP32_P4_LCD4B_VOICE_H
#define BOARD_OVERRIDES_ESP32_P4_LCD4B_VOICE_H

#define HAS_USB_HID false
#define DEBUG_CRASH_API_ENABLED 0
#include "../esp32-p4-lcd4b/board_overrides.h"

#define IS_VOICE_ASSISTANT true

#define PORTAL_PRIMARY_FRAGMENT "voice"
#define PORTAL_PRIMARY_CATEGORY "voice"
#define PORTAL_PRIMARY_LABEL    "Voice Assistant"
#define PORTAL_PRIMARY_ICON     "\xf0\x9f\x8e\xa4"

#endif // BOARD_OVERRIDES_ESP32_P4_LCD4B_VOICE_H