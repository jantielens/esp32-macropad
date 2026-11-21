---
title: ESP32-4848S040
description: RGB panel pin assignments and firmware limits for the ESP32-4848S040.
---

Target `esp32-4848S040` is case-sensitive. It runs Macropad on the ESP32-S3
board with a 4-inch, 480 x 480 ST7701 RGB display and GT911 capacitive touch.
The board has 16 MB flash and 8 MB PSRAM.

## Firmware and USB

Wi-Fi/MQTT, MCP, remote images, and native Extensions are enabled.
USB and BLE keyboard output are not enabled in this target. The presence of
USB-capable silicon does not imply a USB HID port in the firmware.
Audio is not enabled.

Files use internal flash. Native Extensions require ESP32-S3 packages and
have two 56 KiB slots and one 120 KiB slot of usable ELF capacity.
OTA is supported after a full first flash.

For flashing and diagnostic logs, use the board's UART connection at
115200 baud. BOOT/GPIO0 is active low; hold BOOT while resetting, release it,
then flash the exact target through the
[installer](https://jantielens.github.io/esp32-macropad/).
Back up files and packages before installing a different partition layout.

## Configured onboard signals

The RGB panel occupies many GPIOs. This table records firmware wiring, not
available accessory connections. Do not repurpose these signals while using
the display.

| Signal                          | GPIO                  |
| ------------------------------- | --------------------- |
| Backlight                       | 38                    |
| Panel setup SCK / MOSI / CS     | 48 / 47 / 39          |
| RGB DE / VSYNC / HSYNC / PCLK   | 18 / 17 / 16 / 21     |
| R0-R4                           | 11, 12, 13, 14, 0     |
| G0-G5                           | 8, 20, 3, 46, 9, 10   |
| B0-B4                           | 4, 5, 6, 7, 15        |
| Touch SDA / SCL                 | 19 / 45               |

GT911 uses address `0x5D` or `0x14`. No dedicated touch interrupt/reset GPIO
is configured. GPIO0 is also assigned to the panel's red channel; do not treat
it as a spare accessory pin.

## Connector button inputs

The rear 8-pin connector exposes three GPIO inputs for external buttons.
These are not onboard buttons. Use them only with the optional relay module
disconnected; the firmware does not drive relays.

| Portal input | GPIO |
| ------------ | ---- |
| R1           | 40   |
| R2           | 2    |
| R3           | 1    |

Inputs use internal pull-ups and are active low. An external button closes
the selected GPIO to GND. Configure tap and hold actions under
**Actions > Hardware Buttons**, then select **Save Hardware Buttons**.
See [physical-button actions](../../../docs/user/device/triggers.md#physical-buttons)
for shared action behavior.

BOOT/GPIO0 is reserved for serial download recovery. It is not a runtime
action button or a boot-hold configuration-mode button, because reconfiguring
it as an input interferes with the display's highest red-data bit.
Do not press BOOT during normal display operation.

> [!IMPORTANT]
> When upgrading from firmware that exposed BOOT as a runtime button, its
> saved tap and hold actions transfer to R1 (GPIO40). Review or clear those
> actions before connecting an external button.

## References

* [Board support reference](https://github.com/aquaElectronics/esp32-4848s040-st7701)
* [Firmware assignments](board_overrides.h)
* [Build and partition selections](../../../config.sh)
* [Hardware index](../../../docs/user/hardware.md)
