---
title: Matrix Rain Extension
description: Configuration and development guide for the Matrix-style character rain native extension
ms.date: 2026-08-17
ms.topic: reference
---

## Overview

Matrix Rain renders independent streams of green ASCII characters behind a
subtle near-black background. Characters use the firmware's bundled fonts, so
the effect works without packaging a separate glyph asset.

## Configuration

Add the **Extension** widget to a button, select `matrix-rain`, and optionally
provide:

```json
{
  "clock": "[time:%H:%M:%S;Europe/Brussels]",
  "font_family": "doto",
  "font_size": 18,
  "speed": 1.0,
  "density": 0.8,
  "trail_length": 1.0
}
```

| Field | Default | Description |
| --- | --- | --- |
| `font_family` | `default` | `default`, `dseg7`, `bebas`, or `doto`. |
| `font_size` | `18` | Compiled firmware font size from 12 to 48 pixels. |
| `clock` | Disabled | Optional `[time:...]` binding with seconds that displays a centered bright-green `HH:MM` clock. |
| `speed` | `1.0` | Rain speed multiplier from `0.1` to `12.0`. |
| `density` | `0.8` | Portion of columns that contain rain, from `0.0` to `1.0`. |
| `trail_length` | `1.0` | Visible characters per stream multiplier from `0.25` to `2.0`. |

The clock requires room for five character cells at the selected font size. If
the widget is narrower, Matrix Rain leaves the optional clock disabled.

Set the External Widget's `extension_tick_interval_ms` from `33` to `1000`
milliseconds to control animation cadence. Lower values produce smoother
animation and higher CPU usage.

When `clock` is configured, use the firmware's time binding syntax. For
example, `[time:%H:%M:%S;Europe/Brussels]` formats a 24-hour clock in the named
timezone. Seconds are used internally to plan rain-head collisions and are not
displayed. Omitting the timezone uses UTC. The clock requires synchronized NTP
time before it can show the current time.

Clock characters remain bright green and normal rain skips their cells. Matrix
Rain uses the hidden seconds to reserve changing digit lanes and schedules
their normal-looking heads for the next minute boundary. A changed digit updates
when its planned head arrives. If an update remains pending 10 seconds after
the time binding reports the new minute, the clock updates directly to avoid a
stale display.

## Build

```bash
bash tools/build-p4-extension.sh \
  extensions/matrix-rain/matrix_rain.cpp \
  build/extensions/matrix-rain@1.0.0.elf
```

The extension requires firmware and a package built for the same native
extension ABI.