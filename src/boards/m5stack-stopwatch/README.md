---
title: M5Stack StopWatch
description: Hardware revision safety, recovery, controls, and current firmware support for StopWatch.
---

Target `m5stack-stopwatch` runs Macropad on the ESP32-S3R8 StopWatch with
16 MB flash, 8 MB PSRAM, a 1.75-inch round 466 x 466 CO5300 AMOLED, and
CST820B capacitive touch.

## Check the hardware revision first

> [!WARNING]
> Some **v1.0** rear stickers incorrectly label a **5V IN** pin as **BAT**.
> Never connect a battery to that pin. On **v1.0.1**, the revised `*BAT`
> connection is a battery pin. Confirm your revision against the vendor's
> diagrams before attaching rear accessories.

## Power and recovery

* A short press of the power button powers on or resets the unit.
* Two quick presses power it off. Firmware does not remap this button.
* For download mode, connect a data-capable USB-C cable to a computer. Hold
  the power button for about two seconds until the green LED lights, then
  release, following the vendor procedure.
* To recover portal access, hold the **yellow** programmable button through
  the application startup check (at least 1.5 seconds). This enters
  configuration mode without erasing saved settings.

Use the [installer](https://jantielens.github.io/esp32-macropad/) with this exact
target for the first full flash. Files use internal flash; the firmware has
two 4 MiB application slots and supports OTA afterward. Back up before changing
partition layouts. Native USB provides hardware CDC serial, not HID output.

## Display and configured features

Pads use the full square drawing canvas, but the physical circle clips corners.
The maximum pad grid is 5 x 5. Keep important controls inside the visible circle.
Brightness and screen-saver controls operate the AMOLED itself; there is no
backlight GPIO. Use low brightness and avoid long-lived static content.

Wi-Fi/MQTT, remote images, remote diagnostics, and ES8311 speaker/MP3 playback
are enabled. The image library, BLE keyboard output, USB HID, MCP, and native
Extensions are disabled. Microphone capture, IMU, and RTC functions are not
exposed by this target.

Configured peripherals are not a guarantee that every power, charging, motor,
or display mode has been physically validated. Verify recovery and the functions
you intend to use on your revision before relying on battery-powered operation.

## Board actions

Configure yellow/blue tap and hold actions in the portal. The **M5Stack
StopWatch** action group adds:

* **Set charging mode:** Off, Low (nominal 185 mA), or High (nominal 425 mA).
  The boot default is Low. An action changes the current session only.
* **Status LED:** On or Off. Reboot restores the boot level; power-controller
  status signaling can override it.
* **Vibrate:** comma-separated motor/pause durations in milliseconds, for
  example `20,100,20`. Use 1-15 steps with an odd count and a total of at most
  5000 ms, including pauses. Steps must be positive; `0` by itself stops the
  motor. A new pattern replaces an active one.
* **Shutdown:** stops audio and vibration, cancels the remaining actions, and
  powers off. It is rejected during active OTA. It does not save unsaved
  portal edits or finish uploads; save/complete those first.

## Configured onboard signals

These pins are occupied. Use the vendor connector diagrams, including the
revision warning above, for expansion wiring.

| Signal                      | GPIO or controller                        |
| --------------------------- | ----------------------------------------- |
| Yellow / blue button        | 2 / 1, active low                         |
| Shared I2C SDA / SCL        | 47 / 48, 100 kHz                          |
| Display CS / clock          | 39 / 40                                   |
| Display D0 / D1 / D2 / D3   | 41/42/46/45                               |
| Display / touch reset       | M5IOE1 expander, not direct ESP32 GPIOs   |
| Touch interrupt             | 13                                        |
| ES8311 address              | `0x18`                                    |
| Audio MCLK / BCLK / LRCK    | 18 / 17 / 15                              |
| Speaker data                | 21                                        |
| Audio rail / amp enable     | M5IOE1 expander                           |

## References

* [Vendor safety notice, connectors, and recovery](https://docs.m5stack.com/en/core/StopWatch)
* [Firmware assignments](board_overrides.h)
* [Board action definitions](stopwatch_actions.cpp)
* [Hardware index](../../../docs/user/hardware.md)
