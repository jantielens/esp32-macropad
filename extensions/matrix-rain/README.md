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
| `speed` | `1.0` | Rain speed multiplier from `0.1` to `12.0`. |
| `density` | `0.8` | Portion of columns that contain rain, from `0.0` to `1.0`. |
| `trail_length` | `1.0` | Visible characters per stream multiplier from `0.25` to `2.0`. |

Set the External Widget's `extension_tick_interval_ms` from `33` to `1000`
milliseconds to control animation cadence. Lower values produce smoother
animation and higher CPU usage.

## Build

```bash
bash tools/build-p4-extension.sh \
  extensions/matrix-rain/matrix_rain.cpp \
  build/extensions/matrix-rain@1.0.0.elf
```

The extension requires firmware and a package built for the same native
extension ABI.