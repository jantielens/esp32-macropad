---
title: reTerminal Sticky Interactive
description: Interactive e-paper firmware, occupied pins, startup recovery, and validation limits for Seeed reTerminal Sticky.
---

Target `reterminal-sticky-interactive` uses the existing Macropad firmware on
the Seeed reTerminal Sticky: ESP32-S3, 32 MB flash, 8 MB OPI PSRAM, a 3.97-inch
800 x 480 e-paper display, and GT911 touch. This is interactive pad firmware,
not the sleep-first E-Paper Frame.

> [!WARNING]
> This target has no physical hardware acceptance yet. The implementation uses
> the published Rev 01 schematic (2026-06-05) and vendor sources as its baseline,
> not a verified shipping unit. Battery startup, power retention, both panel
> variants, touch orientation, buzzer output, sensors, Wi-Fi, and OTA still
> require device testing.

## Display and controls

The display driver supports the documented SSD1677 and SSD2677 variants.
Black & white is the default, with partial waveforms and periodic/manual full
cleanup. Four-level grayscale always uses full refresh. Animations are disabled,
and there is no backlight control. Follow
[E-paper Presentation](../../../docs/user/specialized/interactive-epaper.md)
for rendering, dithering, cadence, Save/Reboot, and full refresh controls.
VCOM settings are not consumed by this driver.

Focus, navigation, dialog changes and user-triggered variable updates use
interaction-priority presentation:
they request a prompt coherent frame and bypass background settling/cadence.
An active waveform, required full cleanup and recovery still complete normally.
This does not shorten controller preparation or the physical waveform; latency
and repeated-partial image quality still need physical measurement on each
controller variant. Runtime controller probing, including BUSY fallback, remains
enabled.

Keep the buttons at the top for the default landscape orientation.
Touch reads the controller's configured resolution without rewriting it.
A reported 800 x 480 configuration is used directly; a 480 x 800 configuration
is rotated into landscape. Other or unreadable configurations leave touch
unavailable with an error. Verify the center and all four corners before use.

| Button | GPIO | Default runtime tap / hold |
| ------ | ---- | -------------------------- |
| AI/OK  | 4    | Unassigned / unassigned    |
| Up     | 5    | Unassigned / unassigned    |
| Down   | 6    | Unassigned / unassigned    |

All three buttons are active low. Configure runtime actions through the
existing physical-button settings in the portal. For configuration recovery,
hold **Up** through the application's 1.5-second startup check. This opens
configuration mode without erasing settings and is separate from runtime
assignments. No button powers down or sleeps the device by default.

The GPIO48 buzzer supports tone alerts and button feedback through the existing
[audio controls](../../../docs/user/media/audio.md). It plays one pitch at a
time: an active alert takes priority and overlapping feedback is discarded.
Volume changes PWM duty, not a calibrated loudness. MP3, Music, microphone,
and amplifier controls are unavailable. Physical sound and volume still need
device validation.

### Turn off the device

The board-local **reTerminal Sticky > Shutdown** action (`shutdown`) requests
hardware power-off, not deep sleep. Assign it to a pad button or a physical
button hold through the existing action editor; default assignments stay empty.
Use **Save Pad** for pad buttons or **Actions > Hardware Buttons > Save Hardware
Buttons** for physical inputs. Physical-button assignments apply without reboot;
see [trigger settings](../../../docs/user/device/triggers.md#physical-buttons).
Shutdown does not save drafts; finish saves and uploads before invoking it.

Disconnect USB/external power before using Shutdown. Release AI/OK after
triggering it: that button directly keeps the supply enabled while held.
The action fails if external power is present or AI/OK remains held for five
seconds. It also refuses admission during OTA, configuration writes, or a
reserved power/network transition. Put Shutdown last in an action sequence;
successful power removal cannot run subsequent actions.

The Rev 01 schematic requires GPIO46 low, GPIO45 low, then GPIO46 high to
clock the power latch off. If the MCU is still running after 500 ms, firmware
attempts to restore the latch and reports failure instead of entering sleep.
The e-paper image remains visible after power removal and is not an off
indicator. Press AI/OK to supply startup power, or reconnect USB.
Battery-powered shutdown has been user-verified on one unit. Startup button
timing and recovery still require physical validation on the shipping board.

## Provisioning, storage, and updates

Use the existing [setup workflow](../../../docs/user/getting-started.md)
and [update/recovery guidance](../../../docs/user/device/updates-and-recovery.md).
Wi-Fi, the portal, MQTT, MCP, persistence, and OTA use the shared firmware
paths. Files use internal flash, not SD. The reused 32 MB layout has two
8 MiB application slots and a 15.875 MiB internal filesystem.

Serial logging and flashing use the CH343P UART0 bridge at 115200 baud on
GPIO43/44. USB-C does not provide native USB HID in this firmware.
The build selects QIO flash, OPI PSRAM, and UART serial rather than USB CDC.
Back up settings and files before changing firmware or partition layouts;
an application-only OTA does not replace the partition table.

BLE supports the
[Send BTHome event action](../../../docs/user/integrations/home-assistant.md#send-a-bthome-button-event)
independently of MQTT sensor telemetry. This target uses an experimental
broadcast-only BLE profile without scanning or connections. Validate delivery
for the button numbers and event types you use; queue admission does not
confirm Home Assistant receipt. Sustained runtime memory headroom under
MQTT-enabled load and OTA coexistence still require device validation.

SD, microphone, IMU, RTC, BLE HID, USB HID,
native Extensions, remote image fetching, and the image library are disabled.
There is no custom charging policy, gauge calibration, low-battery shutdown,
or battery-runtime guarantee.

## Environmental and battery readings

The SHT40 reports `temperature` in degrees Celsius and `humidity` in percent.
Successful humidity readings are clamped to 0-100%, as recommended by Sensirion.
The BQ27220 reports `battery_voltage` in volts and `battery_percentage` from
the gauge. `external_power_present` indicates whether GPIO9 detects external
power; it does not indicate battery charging. These fields are available through
the sensor API, MQTT health state, Home Assistant discovery, and
`[sensor:...]` bindings. For example, `[sensor:temperature;%.1f|N/A]` displays
the cached temperature.

Readings are unavailable until a valid sample arrives. After a read failure,
the last good value remains visible and may be stale. The SHT40 is sampled
approximately every five seconds and the gauge every two seconds when OTA is
not active. Battery current is not exposed because the shipping gauge's
calibration is unverified. No gauge configuration or calibration is written.

## Occupied onboard connections

These are configured onboard signals, not free accessory pins.

| Function                         | GPIO                         |
| -------------------------------- | ---------------------------- |
| Display SCK / MOSI / shared MISO  | 13 / 14 / 12                 |
| Display CS / DC / reset / BUSY    | 15 / 16 / 17 / 18            |
| Display enable                   | 47, active high              |
| GT911 SDA / SCL (Wire1, 400 kHz)  | 3 / 2                        |
| GT911 INT / reset / enable       | 21 / 41 / 42                 |
| Sensor SDA / SCL (Wire0, 400 kHz) | 1 / 0                        |
| Power hold / power lock          | 45 / 46, high in that order   |
| Charger enable baseline          | 39, low                      |
| External-power input             | 9                            |
| Deferred SD CS / enable / detect | 8 high / 10 low / 11 unused   |
| Deferred microphone enable      | 38, low                      |
| Buzzer                           | 48, PWM tone output          |

The early application hook asserts the power latch before logging and other
startup work. It cannot guarantee retention before the application starts.
Normal board initialization deselects and powers off SD before display SPI
access, powers off the microphone, initializes the sensor bus once under the
existing Wire0 lock, and allows touch/display rails to settle.
Touch alone initializes Wire1; the sensors share the board-owned Wire0 bus.
GPIO9 denotes external power, not proven battery-charging status.

## References

* [Seeed hardware overview](https://www.seeedstudio.com/sticky/docs/en/device-guide/hardware-overview/)
* [Published schematic](https://files.seeedstudio.com/wiki/reterminal_sticky/res/reTerminal_Sticky_Schematic_diagram_260609.pdf)
* [Firmware assignments](board_overrides.h)
* [Build and partition selection](../../../config.sh)
* [Hardware index](../../../docs/user/hardware.md#e-paper)
