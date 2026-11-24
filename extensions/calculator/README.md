---
title: Calculator Extension
description: Configuration and usage guide for the Calculator native extension
ms.topic: reference
---

## Overview

Calculator provides an immediate-execution calculator with arithmetic, percent,
square root, sign, and memory keys. By default, it uses the owning button's
resolved background and text colors.

The keys and spacing adapt to the available widget region. If it is too small,
Calculator displays `Make widget bigger`.

## Configuration

Add the **Extension** widget to a button and select `calculator`. Leave
**Upscale** at 1x: Calculator uses interactive controls, not a canvas. Leave
the configuration empty to use the default appearance.

Key presses follow the device's visual feedback policy and configured tap
sound. Devices without audio, or with tap sounds disabled, remain silent.
Remove `display_style` from older Calculator configurations.

| Field | Default | Description |
| --- | --- | --- |
| `font` | `default` | Inherit Device Typography; use `@builtin` for Montserrat or an uploaded font name. Unavailable fonts fall back to Montserrat. |
| `corner_radius` | `auto` | `auto` uses the default radius of 3 pixels; an integer from 0 to 32767 sets the key radius in pixels. |
| `keys_background` | Button background | Default key background as `#RRGGBB`. |
| `keys_foreground` | Button text color | Default key text and border color as `#RRGGBB`. |
| `keys_overrides` | None | Optional category objects with `background` and/or `foreground` colors. |
| `display_background` | Button background | Result display background as `#RRGGBB`. |
| `display_foreground` | Button text color | Result text color as `#RRGGBB`. |
| `display_border_color` | Display foreground | Result display border color as `#RRGGBB`. |
| `display_corner_radius` | `auto` | `auto` uses 2 pixels; an integer from 0 to 32767 sets the result display radius. |
| `display_font` | `font` | Font for the result display, with the same options as `font`. |
| `display_alignment` | `right` | Result text alignment: `left`, `center`, or `right`. |

For example:

```json
{
  "font": "default",
  "corner_radius": "auto",
  "keys_background": "#FFFFFF",
  "keys_foreground": "#111111",
  "keys_overrides": {
    "numbers": {"background": "#FFFFFF", "foreground": "#111111"},
    "operators": {"background": "#333333", "foreground": "#FFFFFF"},
    "functions": {"background": "#DDDDDD", "foreground": "#111111"},
    "memory": {"background": "#EEEEEE", "foreground": "#111111"}
  }
}
```

`numbers` covers digits and decimal; `operators` covers `+`, `-`, `*`, `/`,
and `=`; `functions` covers `C`, `+/-`, `%`, and `sqrt`; `memory` covers the
four memory keys. Each color falls back independently from category override
to key default to the owning button. Font sizes are selected to fit the
available space.

Style the result display independently:

```json
{
  "display_background": "#DDEECC",
  "display_foreground": "#111111",
  "display_border_color": "#333333",
  "display_corner_radius": 4,
  "display_font": "@builtin",
  "display_alignment": "right"
}
```

You can combine these settings with the key settings. Keep the complete
configuration within the 511-byte limit.

E-paper refresh speed and ghosting depend on the display. Rapid touches may not
appear immediately.

### TI-108-inspired style

For a look inspired by the blue TI-108, set the owning button's background to
dark blue, such as `#1B3568`, and use this configuration:

```json
{
  "font": "@builtin",
  "corner_radius": 3,
  "keys_background": "#F5F5F0",
  "keys_foreground": "#1B3568",
  "keys_overrides": {
    "operators": {"background": "#C53235", "foreground": "#FFFFFF"},
    "functions": {"background": "#C53235", "foreground": "#FFFFFF"}
  },
  "display_background": "#C5D0B8",
  "display_foreground": "#202820",
  "display_border_color": "#1B3568",
  "display_corner_radius": 2
}
```

This approximates the white number and memory keys, red operation keys, and
LCD-style display. It does not reproduce the original layout or typeface.

## Usage

Operations execute as entered: `2 + 3 * 4 =` gives `20`. Percent uses the
left operand for addition and subtraction: `200 + 10 % =` gives `220`, while
`200 * 10 % =` gives `20`. Pressing `=` again repeats the last operation.

`MC` clears memory, `MR` recalls it, and `M+` and `M-` add or subtract the
displayed value. `C` clears the calculation and errors without clearing memory.
Memory and calculations reset when the widget instance restarts, including on
resize or reboot.

Entries accept up to seven digits and results display seven significant digits.
Division by zero, negative square roots, and results outside the supported
range display `Error`; press `C` to continue.

## Build

Build this extension for ESP32-P4:

```bash
bash tools/build-extension.sh p4 \
  extensions/calculator/calculator.cpp \
  build/extensions/calculator@1.1.0-p4.elf
```

Use `s3` or `esp32` and the matching filename suffix for other targets. Build
and sign every shipped extension package:

```bash
./tools/build-p4-extensions.sh
```

See the [Extension guide](../../docs/user/media/extensions.md) for installation
and the [developer guide](../../docs/dev/extensions.md) for signing and ABI
requirements.
