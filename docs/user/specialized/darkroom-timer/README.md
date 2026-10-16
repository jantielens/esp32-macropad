---
title: Configure and use a Darkroom Timer
description: Set relay actions, make timed exposures and f-stop strips, and preserve print records.
---

Darkroom Timer coordinates photographic enlarger exposures, f-stop test strips,
and print records. Configured actions operate external enlarger/safelight
equipment, while optional metering helps establish an exposure starting point.

Use [board notes](../../hardware.md) for sensor/wiring details.
This firmware sends configured actions to external equipment; it is not an
electrical safety interlock. Do not modify mains wiring as part of software
setup. Use appropriately rated, safely installed equipment.

## Verify relay actions first

Open the Darkroom configuration section.
Set one action for each slot: Enlarger On/Off and Safelight On/Off.
For a compatible Shelly HTTP endpoint, supply its host, relay index, and on/off
action. Other supported action types can be configured, but their latency and
delivery semantics still apply. Choose **Save Relay Config**.

Test every on/off path without light-sensitive work in progress.
Keep a physical stop/power control accessible; a queued network request is not
proof the lamp switched.
Do not run exposure, focus, and strip sequences competitively against one lamp.

## Make an exposure

Use supported Expose actions to set/adjust time, Start, Pause/Resume, Stop, or
Toggle. Focus holds the enlarger on for composition; exit it deliberately.
Count-up/countdown bindings can label controls.

Dry-down compensation 0-15% **reduces** effective exposure:
nominal time times (1 minus percentage/100), rounded to tenths.
For example, 10 seconds at 10% becomes 9 seconds.
Use the device's effective-time binding to show what will run.
It does not add exposure or replace a processed-paper calibration.

## Make an f-stop strip

Set base time, stop increment, segment count, inter-segment pause, and optional
tick pattern through the F-stop Strip actions.
Start with scrap material and verify the masking sequence.
You must move the physical mask between segments; the timer does not do this.
Stop the sequence before making unrelated relay changes.

## Meter and log prints

Follow [meter calibration and two-point readings](metering.md) before treating
an estimated exposure as a printable recommendation.
Use live `[expose:...]`, `[strip:...]`, and `[meter:...]` keys advertised by the
device; supported commands appear in its Action reference.

Completed exposures create print records with available exposure/meter context.
In Prints, inspect, star, edit notes, and export records.
Export before Clear All/Delete. The bounded history can evict old records,
including starred ones; a star is not an archive lock.
Configuration backup is not a substitute for exporting print logs.

[Specialized workflows](../README.md) | [User guides](../../README.md)
