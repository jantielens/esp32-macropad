---
title: CrowPanel 4.2 Interactive E-Paper
description: Physical navigation, recovery buttons, display wiring, and firmware limits for the 4.2-inch CrowPanel.
---

Target `crowpanel-4.2-interactive` is for the ESP32-S3 CrowPanel with 8 MB
flash, 8 MB PSRAM, and a 4.2-inch 400 x 300 SSD1683 e-paper display.
It is **black-and-white only**, with no touchscreen or frontlight.
This is interactive pad firmware, not the sleep-first image Frame.

## Physical navigation

The default assignments are:

| Control       | GPIO   | Default runtime action                    |
| ------------- | ------ | ----------------------------------------- |
| Menu          | 2      | Unassigned                                |
| Back          | 1      | Unassigned                                |
| Rotary Up     | 6      | Select previous button                    |
| Rotary Down   | 4      | Select next button                        |
| Rotary OK     | 5      | Activate selected button; hold for hold   |
| BOOT          | 0      | Force full e-paper refresh                |

Buttons are active low. Each pad initially has no selected button; select one
before activating it. Configure the physical-button actions in the portal.
To switch pads from the device, provide pad-navigation actions in your pads.

For configuration recovery, hold **Menu**, not BOOT, through the application's
startup check (at least 1.5 seconds). BOOT/GPIO0 held during reset instead
selects chip download mode.

## Display and firmware limits

Use slow-changing monochrome layouts. Animations are disabled. Changed focus,
pad navigation, dialogs and user-triggered variable updates receive interaction
priority once their complete frame is ready. Background changes retain configured
cadence and coalescing; an active waveform completes first. Startup, recovery,
manual and threshold-triggered full refreshes still flash for ghosting control.
See [presentation settings](../../../docs/user/specialized/interactive-epaper.md).
A gray-looking source color is not native grayscale panel support.

Screenshots are unavailable while a presentation is in progress or incomplete.
OTA/shutdown lets an activated waveform complete before releasing display
resources. A cancelled update requires a full recovery; a BUSY timeout disables
the driver until reboot. The driver requires active-low BUSY assertion and release
within 15 seconds and retains the controller's full-refresh guard delays.
Verify interaction latency, BUSY timing, all rotations and OTA recovery on your
hardware; host tests do not establish panel speed or quality.

Wi-Fi/MQTT and MCP are enabled. Audio, USB/BLE keyboard output, native
Extensions, remote image fetching, and the image library are disabled.
Files use internal flash. The 8 MB layout supports OTA with two 3.5 MiB
application slots and 896 KiB of LittleFS file storage.

When upgrading from the older 3 MiB-slot layout, back up settings/files first.
The application and filesystem boundaries move: install the new layout with a
full USB flash and reinitialize file storage before restoring files. A
firmware-only OTA update cannot migrate the partition table. OTA updates work
normally after the new layout is installed.

For the first full USB flash or recovery, use this exact target in the
[installer](https://jantielens.github.io/esp32-macropad/).
Use the board's UART connection for serial diagnostics at 115200 baud.
Back up settings/files before changing partition layouts.

## Configured panel signals

These are occupied onboard connections, not a free-pin header map.

| Signal               | GPIO             |
| -------------------- | ---------------- |
| SPI clock / MOSI     | 12 / 11          |
| CS / DC / reset      | 45 / 46 / 47     |
| Busy                 | 48, active low   |
| Panel power enable   | 7, active high   |

## References

* [Vendor hardware guide](https://www.elecrow.com/wiki/CrowPanel_ESP32_E-paper_4.2-inch_HMI_Display.html)
* [Firmware assignments](board_overrides.h)
* [Build and partition selections](../../../config.sh)
* [Hardware index](../../../docs/user/hardware.md)
