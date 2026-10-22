---
title: M5Stack Core2
description: Hardware connections, touch-zone buttons, power behavior, and validation limits for the M5Stack Core2 target.
---

Target `m5stack-core2` supports the M5Stack Core2 with 16 MB flash, 8 MB PSRAM,
a 2.0-inch 320 x 240 TFT, FT6336 capacitive touch, an NS4168 speaker amplifier,
and AXP192 power management.

## Hardware revision and validation

The firmware uses TFT_eSPI's ILI9341-compatible initialization with the M5Stack
panel configuration. The Core2 display controller marking has not been recorded
for the unit used with the earlier working firmware. This port has passed a
firmware build but has not yet been tested on hardware. Verify the LCD revision,
display colors and rotation, touch alignment, speaker output, PMIC brightness,
battery readings, and shutdown behavior on the target unit before relying on
them.

## Power and recovery

The AXP192 controls the display backlight and speaker amplifier. The firmware
does not remap the physical power or reset controls; they retain their vendor
behavior. A **Shutdown** button action requests PMIC power-off and is unavailable
during firmware updates, configuration writes, or network transitions. Save
portal changes and finish firmware updates before invoking shutdown.

The target uses a 16 MB flash layout with two 3 MB OTA application slots and
internal filesystem storage. A first install or partition-layout change
requires a full USB flash. OTA updates do not change the partition table.

## Display, touch, and audio

The capacitive touch controller also exposes three virtual touch zones below
the visible display. Configure A, B, and C with the same tap and hold actions as
other hardware buttons. Touches in those zones dispatch actions and are not
sent to the display as pointer presses.

Audio is speaker output only. The target has no microphone input. Volume uses
software gain because the NS4168 amplifier has no volume control interface.
The battery sensor reports voltage, estimated charge percentage, and charging
state from the AXP192.

## Home Assistant and memory limits

MQTT bindings remain available. Direct Home Assistant live bindings, entity
lookup, history hydration, and remote image fetching are disabled on this target.
Their workers require PSRAM task stacks, which the current classic ESP32 SDK
does not allow. They do not fall back to consuming internal RAM. The LVGL task
uses an internal stack.

## Configured connections

| Signal | GPIO or controller |
| --- | --- |
| Display MISO / MOSI / clock | 38 / 23 / 18 |
| Display CS / DC | 5 / 15 |
| Display reset | AXP192 GPIO4 |
| Backlight | AXP192 DCDC3 |
| Shared touch and PMIC I2C SDA / SCL | 21 / 22, 400 kHz |
| FT6336 touch controller | I2C address `0x38` |
| AXP192 | I2C address `0x34` |
| I2S BCLK / LRCK / speaker data | 12 / 0 / 2 |
| Speaker amplifier enable | AXP192 GPIO2 |
| Microphone input | Not connected |

These assignments are occupied. Check the vendor's Core2 documentation before
connecting external hardware or changing power wiring.