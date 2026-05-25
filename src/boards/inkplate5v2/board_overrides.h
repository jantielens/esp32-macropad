#pragma once

// ==========================================================================
// Board Overrides: inkplate5v2 (Soldered Inkplate 5 V2)
// ==========================================================================
// 5.17" 720x1280 3-bit grayscale e-paper, ESP32 classic SoC, 8 MB flash,
// 4 MB QSPI PSRAM, GT911 hardware "wake" button on GPIO36 (external pullup
// on the Inkplate carrier board). Designed for ultra-low-power dashboard
// duty-cycle operation: wake → connect Wi-Fi → optionally fetch a single
// pre-rendered image → refresh panel → deep sleep.

// Project branding — surfaces in mDNS hostname, web portal title, etc.
#define PROJECT_DISPLAY_NAME "Inkplate Dashboard"

// --- Disable all conventional UI / interactive subsystems --------------------
// The Inkplate has no touchscreen and no audio. The e-paper panel is driven
// through the Inkplate library directly, not via the LVGL display HAL, so
// HAS_DISPLAY must stay false to avoid pulling in the colour-display stack
// and to keep portal_components, screens, etc. correctly gated.
#define HAS_DISPLAY false
#define HAS_TOUCH false
#define HAS_AUDIO false
#define HAS_SOUND_PLAYER false
#define HAS_BLE_HID false
#define HAS_BLE false
#define HAS_IMAGE_FETCH false
#define HAS_CUSTOM_FONTS false
#define HAS_BUTTON false  // see HAS_EPAPER_WAKE_BUTTON below for the e-paper-specific wake handler

// Keep MQTT optional for the user; transport defaults remain available.
#define HAS_MQTT true

// --- E-paper device class ---------------------------------------------------
#define HAS_EPAPER true

// Inkplate carrier exposes a GT911-tied "wake" button on GPIO36 (input-only,
// no internal pullup; external pullup is on the board). Wired for ext0
// deep-sleep wake on low level.
#define HAS_EPAPER_WAKE_BUTTON true
#define EPAPER_BUTTON_PIN 36

// Inkplate 5 V2 has no frontlight hardware.
#define HAS_EPAPER_FRONTLIGHT false

// First-boot recovery portal idle default — generous so a user has time to
// finish configuring the dashboard URL after the device falls into Config/AP
// mode on a fresh flash.
#define CONFIG_DEFAULT_PORTAL_IDLE_SECONDS 300

// --- Portal nav: promote the E-Paper page to the primary slot --------------
// The board only has one real configuration story (image URL + rotation),
// so the portal lands there on open and shows it first in the sidebar.
#define PORTAL_PRIMARY_FRAGMENT "epaper"
#define PORTAL_PRIMARY_CATEGORY "epaper"
#define PORTAL_PRIMARY_LABEL    "E-Paper"
// UTF-8 emoji 📰 (newspaper) — single visual hint that this is the e-paper page.
#define PORTAL_PRIMARY_ICON     "\xf0\x9f\x93\xb0"
