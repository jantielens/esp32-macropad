---
title: Nixie Clock Extension
description: Configure the responsive amber Nixie Clock native Extension
ms.date: 2026-08-18
ms.topic: how-to
---

## Overview

Nixie Clock is a native Extension for ESP32-P4 boards. It renders an amber
Nixie-tube clock inside its containing button. The clock retains the artwork's
aspect ratio, centers itself in the available button content, and fills unused
space with black.

The package uses precomposited RGB565 artwork, so use a dark or black button
background. By default, changed digits briefly strike brighter, separators
gently breathe, and one tube occasionally performs a small brightness dip.

## Install and add the widget

1. Install `nixie-clock@1.0.0.ext` in the device portal's **Extensions** page.
2. Add or edit a pad button.
3. Choose the **Extension** widget and select **Nixie Clock**.
4. Set the Extension configuration JSON to an HH:MM or HH:MM:SS time binding.

## Configure time

Use the existing `[time:format;timezone]` binding syntax in the `time` field.
The Extension retains numeric characters from the binding result:

* Four digits render as `HH:MM`.
* Six digits render as `HH:MM:SS`.

### Hours and minutes

```json
{"time":"[time:%H%M;Europe/Brussels]"}
```

### Hours, minutes, and seconds

```json
{"time":"[time:%H%M%S;Europe/Brussels]"}
```

The default is `[time:%H%M]`. Use a valid IANA timezone name when the displayed
time should differ from the device's configured timezone.

## Configure animation

All animation effects are enabled by default but remain deliberately subtle.
Disable every effect with:

```json
{"time":"[time:%H%M;Europe/Brussels]","animations":false}
```

Configure individual effects with these optional numeric settings. Set an
effect's intensity to `0` to disable it without disabling the other effects.

| Setting | Default | Range | Effect |
| --- | ---: | ---: | --- |
| `transition_intensity` | 15 | 0-80 | Brightness boost, as a percentage, when a digit changes |
| `transition_ms` | 180 | 0-2000 | Duration of the changed-digit brightness settle |
| `separator_intensity` | 70 | 0-100 | Depth, as a percentage, of the separator breathing cycle |
| `separator_period_ms` | 1000 | 0-10000 | Separator breathing cycle duration |
| `flicker_intensity` | 10 | 0-80 | Brightness dip, as a percentage, for a rare digit or separator flicker |
| `flicker_duration_ms` | 80 | 0-1000 | Flicker duration |
| `flicker_min_interval_s` | 15 | 0-3600 | Minimum delay before a random flicker |
| `flicker_max_interval_s` | 90 | 0-3600 | Maximum delay before a random flicker |

For example, this keeps digit strikes while disabling breathing and random
flicker:

```json
{
  "time":"[time:%H%M%S;Europe/Brussels]",
  "transition_intensity":20,
  "transition_ms":220,
  "separator_intensity":0,
  "flicker_intensity":0
}
```

## Layout and image quality

The Extension applies one scale factor to the complete clock scene. It scales
down to fit square, portrait, and landscape buttons without stretching. It does
not enlarge its canonical artwork, so especially large buttons retain black
borders rather than showing a pixelated enlarged clock.

The extension package embeds 96 x 147 pixel Nixie digit sprites. The canvas
buffer uses RGB565 and is allocated through the Extension host, which prefers
PSRAM. Its 50 ms tick supports smooth effects. When every effect is disabled or
the clock is otherwise idle, the tick performs no canvas work.

## Build from source

Regenerate the extension artwork after changing the source image or asset
settings:

```bash
python3 tools/generate-nixie-extension-assets.py
bash tools/build-p4-extension.sh \
  extensions/nixie-clock/nixie_clock.cpp \
  build/extensions/nixie-clock@1.0.0.elf
```

The generated assets are documented in [the Nixie artwork guide](../../assets/nixie/README.md). See the [native Extension developer guide](../../docs/dev/extensions.md) for package signing and upload details.