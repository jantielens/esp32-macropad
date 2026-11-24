---
title: reTerminal Sticky Interactive
description: Interactive e-paper firmware, occupied pins, startup recovery, and validation limits for Seeed reTerminal Sticky.
---

Target `reterminal-sticky-interactive` uses the existing Macropad firmware on
the Seeed reTerminal Sticky: ESP32-S3, 32 MB flash, 8 MB OPI PSRAM, a 3.97-inch
800 x 480 e-paper display, and GT911 touch. This is interactive pad firmware,
not the sleep-first E-Paper Frame.

> [!WARNING]
> Complete hardware acceptance remains outstanding. The implementation follows
> the published Rev 01 schematic (2026-06-05) and vendor sources. SSD1677 display
> operation has received limited device testing; SSD2677 remains physically
> unverified. Battery startup, power retention, touch orientation, buzzer output,
> sensors, Wi-Fi, and OTA still require qualification.

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
Interaction priority does not shorten the physical waveform; latency
and repeated-partial image quality still need physical measurement on each
controller variant. Runtime controller probing, including BUSY fallback, remains
enabled.

SSD1677 experimentally reuses controller preparation only between consecutive
successful B&W partial updates. The first partial after a full refresh still
prepares normally. Full cleanup, grayscale, reset, power loss and interrupted
updates invalidate reuse; each image plane still transfers the complete frame.
Repeated-partial image integrity and ghosting with retained preparation require
device validation. Keep a known-good firmware build for rollback.

SSD2677 experimentally reuses preparation between successful updates with the
same render mode, full/partial intent and temperature-selected waveform.
Temperature is read afresh every time; waveform latching, its settle delay,
full-frame transfer and checked drive power-off remain unchanged. A key change
or interrupted update requires reset/setup. Retained-state temperature reads,
image quality and preparation savings remain unverified on SSD2677 hardware.

Physical refresh completion requires an observed BUSY assertion and release.
Missing assertion or a timeout leaves the displayed-frame snapshot unavailable
and requires a full-refresh retry. OTA or stop requests during an active
waveform wait for bounded draining and required drive cleanup rather than
publishing an incomplete image. These safeguards still need device validation
for both controller variants.

Display writes use an experimental 20 MHz SPI clock; reads remain at 4 MHz.
Image integrity at the higher clock still requires validation on each panel
variant. If corruption appears, restore the preserved 10 MHz firmware; there
is no automatic visual-error detection or speed fallback.

Uploads use bounded batches and cooperative sleeps to reduce transfer overhead,
not panel waveform duration. Image integrity and touch responsiveness with the
larger bursts still need validation on each controller variant.

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
assignments. Configuration recovery overrides saved Offline mode, tries saved
Wi-Fi, and starts an AP if connection fails; it does not immediately force AP.
No button powers down or sleeps the device by default.

The GPIO48 buzzer supports tone alerts and button feedback through the existing
[audio controls](../../../docs/user/media/audio.md). It plays one pitch at a
time: an active alert takes priority and overlapping feedback is discarded.
Volume changes PWM duty, not a calibrated loudness. MP3, Music, and amplifier
controls are unavailable. Physical sound and volume still need device validation.

The onboard microphone supports
[microphone level bindings](../../../docs/user/pads/data-sources.md#read-local-sensors-and-health).
Local Voice Notes requires the
[SD-primary variant](../reterminal-sticky-interactive-sd/README.md).

### Turn off the device

Battery-powered operation defaults to automatic shutdown after **300 seconds**
without user activity. Configure it under **Device > Power Savings**; enter
`0` to disable. External power suspends it. See
[automatic shutdown](../../../docs/user/device/display-and-sleep.md#save-battery-power-with-automatic-shutdown)
for activity, save behavior, and timer limitations.

The board-local **reTerminal Sticky > Shutdown** action (`device.shutdown`) requests
hardware power-off, not deep sleep. Assign it to a pad button or a physical
button hold through the existing action editor; default assignments stay empty.
Use **Save Pad** for pad buttons or **Actions > Hardware Buttons > Save Hardware
Buttons** for physical inputs. Physical-button assignments apply without reboot;
see [trigger settings](../../../docs/user/device/triggers.md#physical-buttons).
Shutdown does not save drafts; finish saves and uploads before invoking it.

To leave a clean power-off notice on the e-paper display, configure one
**Shutdown** action with **Shutdown message (optional)** set to `Powered off`
and **Delay before shutdown (ms)** set to `5000`. Save the pad or hardware
buttons as above. The message uses the notification appearance, has no expiry,
and remains on screen after power-off. The non-blocking delay leaves rendering
and button audio feedback running. After the message's physical presentation
and the configured minimum delay, measured from showing the message, shutdown
performs a final full refresh. Further display updates are held until power-off,
so no later partial refresh changes the retained image. Audio completion is not
tracked separately. If preparation does not finish within 60 seconds, shutdown
fails instead of removing power without an acknowledged message.

The message defaults to empty and the delay to `0`, preserving immediate
shutdown for existing assignments. Delay accepts `0` to `30000` ms and also
works without a message. Replacing or dismissing the shutdown message cancels
preparation. A failed shutdown clears only its own notice. Automatic shutdown
uses the same persistent notice, but waits for presentation acknowledgement
and a one-second settling interval before the final full refresh and power-off.
Cancellation or failure releases the display hold.

Disconnect USB/external power before using Shutdown. Release AI/OK after
triggering it: that button directly keeps the supply enabled while held.
The action fails if external power is present or AI/OK remains held for five
seconds. It also refuses admission during OTA, configuration writes, or a
reserved power/network transition. Put Shutdown last in an action sequence;
successful power removal cannot run subsequent actions.
External power is checked before showing the shutdown message and again before
power removal. OTA, protected writes, or power/network transitions that start
during the configured delay cancel preparation rather than being interrupted.

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
paths. The standard target stores files in internal flash. The
[SD-primary variant](../reterminal-sticky-interactive-sd/README.md) stores
files on a required microSD card instead. Both use a 32 MB layout with two
8 MiB application slots, a 15.625 MiB internal filesystem, and three native
Extension slots.

Install signed ESP32-S3 Extension packages in two 56 KiB slots and one
120 KiB slot. Follow [Extension installation](../../../docs/user/media/extensions.md)
and prefer slow-changing content on e-paper. Extension memory use, display
responsiveness, and coexistence with Wi-Fi, BLE, and Voice Notes require
physical validation.

> [!WARNING]
> The Extension layout shrinks the previous internal filesystem. Back up files
> before switching layouts. Erase the old internal storage volume and flash
> the complete firmware and partition table over USB before restoring files;
> application-only OTA cannot change the partition table. An old larger
> filesystem can cause a reboot loop under the new layout.

Serial logging and flashing use the CH343P UART0 bridge at 115200 baud on
GPIO43/44. USB-C does not provide native USB HID in this firmware.
The build selects QIO flash, OPI PSRAM, and UART serial rather than USB CDC.
Back up settings and files before changing firmware or partition layouts;
an application-only OTA does not replace the partition table.

BLE supports the
[Send BTHome event action](../../../docs/user/integrations/home-assistant.md#send-a-bthome-button-event)
independently of MQTT sensor telemetry. This target selects the shared
[NimBLE-Arduino broadcast profile](../../../docs/dev/build-and-release-process.md#ble-backend-and-broadcast-profile)
without scanning, connections, or HID. Validate delivery
for the button numbers and event types you use; queue admission does not
confirm Home Assistant receipt. Sustained runtime memory headroom under
MQTT-enabled load and OTA coexistence still require device validation.

The shared [Offline mode](../../../docs/user/device/network-and-access.md#boot-offline)
supports local pad and physical-button BTHome controls without starting Wi-Fi.
BLE prepares before runtime actions. Verify startup to
first usable physical button and first received advertisement on hardware;
a retained e-paper image or queued event does not prove readiness or delivery.
Offline boot, AP/station switching with BLE, recovery, temporary RAM reclamation,
and interaction latency during connection attempts remain user hardware checks.

IMU, BLE HID, USB HID, remote image fetching, and the image library are disabled.
There is no custom charging policy, gauge calibration, low-battery shutdown,
or battery-runtime guarantee.

## Offline clock

The onboard PCF8563 RTC is enabled at I2C address `0x51`, sharing Wire0
with the environmental sensor and battery gauge. Connect to Wi-Fi with
internet access once and wait for time synchronization before relying on
offline clock labels or alarms. NTP updates the RTC automatically; there
is no separate setting to save or manual clock-setting control.
See [device time](../../../docs/user/device/time.md#time-without-wi-fi).

At boot, valid RTC time restores the system clock without starting Wi-Fi.
An unset clock, a voltage-loss indication, an invalid calendar, or an I2C
failure leaves time unavailable until network synchronization succeeds.
Logs distinguish RTC restoration, NTP synchronization, and RTC write failures.
An `RTC updated` log confirms that synchronized time reached the hardware
clock. Writes wait during OTA or hardware power-off activity; clock readiness
alone does not confirm a completed RTC write.
If an RTC write fails, the running system clock stays usable, but retention
of the corrected time is not confirmed.

The Rev 01 schematic supplies the RTC through D21/D22 from the main 3.3 V
rail or a dedicated always-on `RTC_3V3` regulator (U4) powered by `VSYS`.
That supply is separate from the main power latch, so time survives
hardware Shutdown while the battery can supply the RTC. Offline reboot
restoration and clock advancement during hardware Shutdown have been
user-verified on one unit. A disconnected or
fully depleted battery cannot guarantee retained time; reconnect Wi-Fi to
synchronize again. The RTC can drift during extended offline use.

## Environmental and battery readings

The SHT40 reports `temperature` in degrees Celsius and `humidity` in percent.
Successful humidity readings are clamped to 0-100%, as recommended by Sensirion.
The BQ27220 reports `battery_voltage` in volts and `battery_percentage` from
the gauge, plus signed battery current, calculated battery power, readable
status and native time-to-empty/time-to-full estimates in minutes.
`external_power_present` indicates whether GPIO9 detects external
power; it does not indicate battery charging. These fields are available through
the sensor API, MQTT health state, Home Assistant discovery, and
`[battery:...]` bindings for battery information and `[sensor:...]` for
environmental readings. For example, `[sensor:temperature;%.1f|N/A]` displays
the cached temperature.

Readings are unavailable until a valid sample arrives. After a read failure,
environmental values retain their last good sample. The SHT40 is sampled
approximately every five seconds and the gauge every two seconds when OTA is
not active. Battery measurements expire after six seconds without a successful
sample, excluding observed OTA pauses. Invalid or inapplicable status/runtime
clears immediately. Use `[battery:discharge_power;%.2f|N/A]` for battery draw
and `[battery:time_remaining;%.0f|N/A]` for estimated remaining minutes.
See [shared battery semantics](../../../docs/user/pads/data-sources.md#show-battery-information).

Current comes from `Current()`, updated by the gauge every second; it is not a
peak measurement or `AverageCurrent()`. Power is calculated from that sample's
voltage and current, not total USB/device input power. For the stock gauge
profile, identified by a raw `DesignCapacity()` of 7500, current is divided
by 10 as recommended by [Seeed's battery example](https://github.com/Seeed-Projects/sticky-esp-idf-peripherals/blob/main/main/demo_battery.cpp).
Current and power are unavailable when the
profile differs or cannot be read. The profile check does not verify
calibration; compare current, power and runtime estimates with external
measurements. No gauge configuration or calibration is written.

## Occupied onboard connections

These are configured onboard signals, not free accessory pins.

| Function                         | GPIO                         |
| -------------------------------- | ---------------------------- |
| Display SCK / MOSI / shared MISO  | 13 / 14 / 12                 |
| Display CS / DC / reset / BUSY    | 15 / 16 / 17 / 18            |
| Display enable                   | 47, active high              |
| GT911 SDA / SCL (Wire1, 400 kHz)  | 3 / 2                        |
| GT911 INT / reset / enable       | 21 / 41 / 42                 |
| Sensor/RTC SDA / SCL (Wire0, 400 kHz) | 1 / 0                     |
| Power hold / power lock          | 45 / 46, high in that order   |
| Charger enable baseline          | 39, low                      |
| External-power input             | 9                            |
| SD CS / enable / detect           | 8 / 10 / 11                  |
| Microphone clock / data / enable | 19 / 20 / 38                 |
| Buzzer                           | 48, PWM tone output          |

The early application hook asserts the power latch before logging and other
startup work. It cannot guarantee retention before the application starts.
Normal board initialization deselects and powers off SD before display SPI
access. The SD-primary variant powers it on before mounting; card detect is
unused. Initialization powers off the microphone until capture, initializes the sensor bus once
under the existing Wire0 lock, and allows touch/display rails to settle.
Touch alone initializes Wire1; the sensors share the board-owned Wire0 bus.
GPIO9 denotes external power, not proven battery-charging status.

## References

* [Seeed hardware overview](https://www.seeedstudio.com/sticky/docs/en/device-guide/hardware-overview/)
* [Published schematic](https://files.seeedstudio.com/wiki/reterminal_sticky/res/reTerminal_Sticky_Schematic_diagram_260609.pdf)
* [Firmware assignments](board_overrides.h)
* [Build and partition selection](../../../config.sh)
* [Hardware index](../../../docs/user/hardware.md#e-paper)
