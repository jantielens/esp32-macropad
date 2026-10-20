---
title: Build a world-clock pad
description: Show four independent timezones with current date, optional font style, and appropriate display cadence.
---

A world-clock pad shows local times and dates for several locations together.
Each button uses an explicit timezone, so the locations can follow their own
date and daylight-saving rules without changing the device-wide timezone.

Verify [network time](../device/time.md).
Make a 2x2 pad and use these center labels:

| Button   | Center label                    | Top label   |
| -------- | ------------------------------- | ----------- |
| New York | `[time:%H:%M;America/New_York]` | `New York`  |
| London   | `[time:%H:%M;Europe/London]`    | `London`    |
| Tokyo    | `[time:%H:%M;Asia/Tokyo]`       | `Tokyo`     |
| Sydney   | `[time:%H:%M;Australia/Sydney]` | `Sydney`    |

Put `[time:%a %d %b;ZONE]` in each bottom label, replacing ZONE with its
corresponding timezone. Dates can differ across the grid.
Explicit timezone parameters stay independent of the device-wide timezone.

## Style and verify

Set a readable center style such as `font_size:32;align:center`.
An uploaded font additionally needs a valid font ID; follow
[label styles](../pads/appearance.md#use-label-style-syntax).
Save Pad and compare all four times/dates against a trusted clock.
Unavailable time immediately after boot needs synchronization, not a fabricated
fallback timestamp.

Use `%H:%M:%S` only if second-level updates are useful.
For [interactive e-paper](../specialized/interactive-epaper.md), prefer minutes
and enable minute-boundary refresh.
This pad does not apply to image-only Frame firmware; render the clocks on
its image server instead.

[Examples](README.md) | [User guides](../README.md)
