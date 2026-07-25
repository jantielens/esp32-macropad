#pragma once

// ==========================================================================
// Board Overrides: reterminal-e1003 (Seeed reTerminal E1003)
// ==========================================================================
// 10.3" 1404x1872 16-level grayscale e-paper (IT8951 controller, GC16
// waveform), ESP32-S3 (XIAO form factor), 32 MB flash, 8 MB OPI PSRAM. A
// hardware "Refresh" button plus two nav buttons, capacitive touch, buzzer,
// SHT4x temp/humidity sensor, microSD (shares the panel SPI bus), and a
// battery ADC. The MVE drives the panel through Seeed_GxEPD2 (Adafruit_GFX)
// for an ultra-low-power dashboard duty-cycle: wake -> connect Wi-Fi ->
// fetch a single pre-rendered JPEG -> refresh panel -> deep sleep.

// Project branding is derived from device class (HAS_EPAPER/HAS_DISPLAY)
// via src/app/device_class_branding.{h,cpp}, so no per-board override here.

// --- Disable all conventional UI / interactive subsystems --------------------
// The reTerminal E1003 e-paper panel is driven through Seeed_GxEPD2 directly,
// not via the LVGL display HAL, so HAS_DISPLAY must stay false to avoid
// pulling in the colour-display stack and to keep portal_components, screens,
// etc. correctly gated. Touch, nav buttons, buzzer and the SHT4x sensor are
// out of scope for the MVE.
#define HAS_DISPLAY false
#define HAS_TOUCH false
#define HAS_AUDIO false
#define HAS_SOUND_PLAYER false
#define HAS_BLE_HID false
#define HAS_BLE true
#define HAS_IMAGE_FETCH false
#define HAS_CUSTOM_FONTS false
#define HAS_BUTTON false  // see HAS_EPAPER_WAKE_BUTTON below for the e-paper-specific wake handler

// Keep MQTT optional for the user; transport defaults remain available.
#define HAS_MQTT true

// --- E-paper device class ---------------------------------------------------
#define HAS_EPAPER true

// --- microSD image cache (shares the panel HSPI bus) -----------------------
// The microSD slot reuses the IT8951 SPI bus (SCK=7 / MISO=8 / MOSI=9) with
// its own chip-select. SD_EN gates card power (drive HIGH ~5 ms before mount)
// and SD_DET reads LOW when a card is inserted. Defining EPAPER_SD_CS_PIN
// compiles in the optional SD image cache in the e-paper driver, which lets a
// cache hit skip the multi-second HTTP image download. Disabled at runtime by
// default; enable via the portal (Image & Schedule -> "Cache images on SD").
#define EPAPER_SD_CS_PIN  14
// microSD power-enable gate (drive HIGH ~5 ms before mounting the card).
#define EPAPER_SD_EN_PIN  39
// microSD card-detect input (reads LOW when a card is inserted).
#define EPAPER_SD_DET_PIN 15

// A front-panel user button wakes the device from deep sleep. The E1003 has
// three user buttons wired to RTC-capable ESP32-S3 GPIOs, all active-low with
// onboard pull-ups (read LOW when pressed), which matches the ext1 wake level
// (ANY_LOW) the e-paper class arms. We use KEY0 (GPIO3) — the right "green"
// primary button — as the refresh/config wake button.
//   KEY0 = GPIO3 (right / green)   KEY1 = GPIO4 (middle)   KEY2 = GPIO5 (left)
// Source: Seeed reTerminal E-Series Arduino peripherals cookbook.
#define HAS_EPAPER_WAKE_BUTTON true
#define EPAPER_BUTTON_PIN 3

// --- Battery monitoring (E1003) --------------------------------------------
// Battery voltage is read on GPIO1 (ADC) behind a 2:1 divider, gated by an
// enable pin that must be driven HIGH a few ms before sampling. On the E1003
// that enable pin is GPIO40 (it is GPIO21 on the other reTerminal E models).
// ADC pin for the battery sense divider (GPIO1 on the E1003).
#define EPAPER_BATTERY_ADC_PIN 1
// Drive HIGH ~5ms before sampling to gate the battery divider on (GPIO40 on E1003).
#define EPAPER_BATTERY_ENABLE_PIN 40
// Voltage divider ratio applied to the raw ADC millivolt reading.
#define EPAPER_BATTERY_DIVIDER 2.0f

// The reTerminal E1003 has no frontlight hardware.
#define HAS_EPAPER_FRONTLIGHT false

// The IT8951 manages VCOM internally — there is no user-programmable bias, so
// the portal VCOM page is hidden on this board.
#define HAS_EPAPER_VCOM false

// --- E-paper refresh speed classification --------------------------------
// The IT8951 GC16 full refresh on this 10.3" panel completes in ~1-3 s, fast
// enough that showing transient splashes for state changes is a UX win rather
// than a tax. Enables: "Refreshing" splash on button wake, and the immediate
// "Configuration Mode — preparing…" ack at boot on long-press for config.
#define EPAPER_FAST_REFRESH true

// First-boot recovery portal idle default — generous so a user has time to
// finish configuring the dashboard URL after the device falls into Config/AP
// mode on a fresh flash.
#define CONFIG_DEFAULT_PORTAL_IDLE_SECONDS 300

// --- Portal nav: promote the E-Paper category to the primary slot ----------
// The board's primary configuration story lives under the E-Paper category
// (Status / Image & Schedule / Status Overlay / VCOM). Land on the Status
// page since it gives the user an immediate "is this thing working" view.
#define PORTAL_PRIMARY_FRAGMENT "epaper-status"
#define PORTAL_PRIMARY_CATEGORY "epaper"
#define PORTAL_PRIMARY_LABEL    "E-Paper"
// UTF-8 emoji 📰 (newspaper) — single visual hint for the e-paper category.
#define PORTAL_PRIMARY_ICON     "\xf0\x9f\x93\xb0"
