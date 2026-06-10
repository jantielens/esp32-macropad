#pragma once

// ==========================================================================
// Board Overrides: inkplate6flick (Soldered Inkplate 6FLICK)
// ==========================================================================
// 6.0" 1024x758 3-bit grayscale e-paper, ESP32 classic SoC, hardware "wake"
// button on GPIO36 (external pullup on the Inkplate carrier board). Driven by
// the same Soldered InkplateLibrary 3-bit code path as the Inkplate 5 V2.
// Designed for ultra-low-power dashboard duty-cycle operation: wake → connect
// Wi-Fi → optionally fetch a single pre-rendered image → refresh panel → deep
// sleep.

// Project branding is derived from device class (HAS_EPAPER/HAS_DISPLAY) via
// src/app/device_class_branding.{h,cpp}, so no per-board override here.

// --- Disable all conventional UI / interactive subsystems --------------------
// The e-paper panel is driven through the Inkplate library directly, not via
// the LVGL display HAL, so HAS_DISPLAY must stay false to avoid pulling in the
// colour-display stack and to keep portal_components, screens, etc. correctly
// gated. The 6FLICK has a capacitive touchscreen, but it is unused in the
// dashboard duty-cycle flow.
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

// Inkplate carrier exposes a "wake" button on GPIO36 (input-only, no internal
// pullup; external pullup is on the board). Wired for ext0 deep-sleep wake on
// low level.
#define HAS_EPAPER_WAKE_BUTTON true
#define EPAPER_BUTTON_PIN 36

// The Inkplate 6FLICK has a frontlight, but it is intentionally left disabled
// here to keep the duty-cycle wake path minimal.
#define HAS_EPAPER_FRONTLIGHT false

// TPS65186 PMIC stores a user-programmable VCOM bias — expose the portal page.
#define HAS_EPAPER_VCOM true

// --- E-paper refresh speed classification --------------------------------
// The Inkplate 6FLICK (1024x758 3-bit) completes a full waveform in ~1.26 s,
// fast enough that showing transient splashes for state changes is a UX win
// rather than a tax. Enables: "Refreshing" splash on button wake, and the
// immediate "Configuration Mode — preparing…" ack at boot when the user
// long-presses for config.
#define EPAPER_FAST_REFRESH true

// First-boot recovery portal idle default — generous so a user has time to
// finish configuring the dashboard URL after the device falls into Config/AP
// mode on a fresh flash.
#define CONFIG_DEFAULT_PORTAL_IDLE_SECONDS 300

// --- Portal nav: promote the E-Paper category to the primary slot ----------
// The board's primary configuration story lives under the E-Paper category
// (Status / Image & Schedule / Status Overlay / VCOM). Land on the Status page
// since it gives the user an immediate "is this thing working" view.
#define PORTAL_PRIMARY_FRAGMENT "epaper-status"
#define PORTAL_PRIMARY_CATEGORY "epaper"
#define PORTAL_PRIMARY_LABEL    "E-Paper"
// UTF-8 emoji 📰 (newspaper) — single visual hint for the e-paper category.
#define PORTAL_PRIMARY_ICON     "\xf0\x9f\x93\xb0"
