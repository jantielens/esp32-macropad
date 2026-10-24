---
title: Labels, icons, colors, and appearance
description: Style a button with inherited defaults, label syntax, patterned fills, conditional state, and dynamic colors.
---

Appearance settings control how pads and buttons look: their labels, icons,
colors, and spacing. Shared defaults provide a consistent starting point,
while pad and button settings let you customize individual parts of the layout.

## Choose the level to change

Set shared appearance under **Pads > Pad and Button Defaults**.
A button's explicit field overrides device defaults, which override firmware
defaults. Pad backgrounds have their own pad override and device default.
Use the reset link beside an overridden field to inherit again.

Label styles are selected as whole strings, not merged property by property.
Within that effective style, omitted font family and size independently inherit
device typography. Appearance changes apply to inheriting pads live; device
font/base-size changes require reboot. Manage files in [Fonts](../media/fonts.md).
Template pads fill buttons/bindings, not a second appearance cascade.

## Add labels and icons

Button **Content** provides Top, Center, and Bottom labels.
Mix text and [bindings](bindings.md); enter `\n` for a line break.
Use **fx** to search the device's Binding reference and insert a token.

Choose an icon in the button editor. **From** offers **All icons**, **Symbols**
(Material Symbols), **Emoji** (Noto), or **Image file**. Search and category
filters narrow the icon library; fetching new library artwork requires an
internet connection. Catalog results show artwork on the button's background
color. **Image file** accepts one static PNG, JPEG, or WebP from
the file chooser, a drop, or an image paste. Inputs can be up to 5 MiB and
8 million pixels.

Local images are normalized to PNG before they are retained on the device.
**Downscale to** limits the longest side: Compact (128 px), Standard (256 px,
the default), Large (512 px), or Maximum (720 px). Aspect ratio is preserved,
small images are never enlarged, and the normalized PNG must fit within 512 KiB.
The preview shows the actual dimensions and file size. You can change resolution
from the original file while the dialog stays open. After closing it, choose the
original image again to change resolution. Pad exports include the normalized
artwork, not the original file. This setting does not change the button's layout
or its **Icon Size %**.

After a pad is saved, its retained source can regenerate icons offline when you
resize or copy buttons. Material Symbols follow the button's foreground color;
Noto emoji and local images keep their original colors. **Apply** updates the
button draft; **Cancel** leaves it unchanged. Remove icon clears the editor
draft, and **Save Pad** persists the change. Icon Size 0 is automatic; 1-250%
overrides it.
Position Above/Left/Centered changes ordinary label/icon layout.
Some widgets own placement and hide that selector.

Material Symbols are distributed under the [Apache License 2.0](https://github.com/google/material-design-icons/blob/master/LICENSE).
Noto Emoji artwork is distributed under the [SIL Open Font License 1.1](https://github.com/googlefonts/noto-emoji/blob/main/LICENSE).

## Use label style syntax

Open **Aa** beside a label and enter `key:value;key:value`.
The field holds 127 characters; use a named binding for long color expressions.

| Property       | Supported input                                                               |
| -------------- | ----------------------------------------------------------------------------- |
| `font`         | Uploaded ID or `@builtin`; omit to inherit                                    |
| `font_size`    | Whole pixels, 1-65535; absolute, not proportional                             |
| `font_metrics` | `bounds` or multiplier 1-65535, including decimals; TTF vertical metrics only |
| `font_upscale` | 1.0-2.0; bitmap transformation, including TTF fallback                        |
| `align`        | `left`, `center`, `right`                                                     |
| `x`, `y`       | Pixel offsets, -999 to 999                                                    |
| `mode`         | `clip`, `scroll`, `dot`, `wrap`                                               |
| `color`        | `#RGB`, `#RRGGBB`, or a binding                                               |

```text
font_size:24;align:left;mode:dot
x:10;y:-4;align:right
color:[expr:[net:any]?"#22c55e":"#94a3b8"]
```

Per-label color wins over button text color. Without an override, labels are
centered with automatic size and clipped overflow. Large text does not
automatically fit; check wrapping and font [memory limitations](../media/fonts.md).

## Set geometry and colors

In **Appearance**, set background, text/icon, and border colors.
The shared picker accepts literal hex or a binding where supported and can
generate a [threshold expression](expressions.md#map-thresholds-to-colors).
Set a neutral fallback/default color for startup or unavailable data.
Selecting a palette or recently used color keeps the picker open. Choose
**Apply** to update the draft; closing the picker without applying discards its
selection. Still **Save Pad** to keep your changes.

Border width accepts 0-10 px, radius 0-50 px, and padding 0-50 px.
UI offset is `x;y` pixels, for example `20;-10`.
Button shadow can inherit/enable/disable; type and sizing are device-wide.
Grid spacing, edge insets, and burn-in movement are device-wide layout settings.
Do not use pad-local named bindings in a shared device background default.

## Add a pattern

Supported background/bar pickers offer None, Diagonal stripes, Reverse diagonal
stripes, Crosshatch, Dots, and Checkerboard with separate base/pattern colors.
Spacing is 2-64 logical px; stroke is 1-8 px, no greater than spacing.
Dots use stroke as diameter; Checkerboard uses spacing and ignores stroke.
Geometry is static while supported colors can be live-bound.

Images cover these fills, including letterbox bands; do not assume PNG
transparency reveals a pattern. Gauge fills are unpatterned.
Track outlines and bar corner radius belong to their widget's Content controls.
Color badges are representative previews, not a live device simulation.

## Set button state

Button State accepts `enabled`, `disabled`, or `hidden`, including a binding:

```text
[expr:[mqtt:printer/state]=="online"?"enabled":"hidden"]
```

Disabled buttons are visible but not interactive. Hidden buttons leave their
grid positions empty. Unresolved/error state defaults to enabled, so this is
not an authorization control or safety interlock.
Save and test both available and missing-data states.

[Pad tasks](README.md) | [User guides](../README.md)
