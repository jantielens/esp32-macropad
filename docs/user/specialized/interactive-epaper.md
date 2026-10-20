---
title: Configure Interactive E-Paper
description: Tune physical refresh, black/white conversion, clock cadence, and ghosting for interactive pads.
---

Interactive E-Paper runs pads, bindings, and widgets while controlling when
their changes reach the physical panel. Its presentation settings balance
freshness against panel refresh work and ghosting, rather than treating the
display like a continuously refreshed LCD.

First create a [saved pad](../pads/editing.md).
Open **Display > E-paper Presentation**.
The panel is not a continuously refreshed LCD: successive UI changes can combine
into one physical presentation, and full refresh can visibly flash.

## Choose rendering

Select a presentation mode supported by the device.
Black & white offers these conversions:

* Threshold maps pixels at/above the cutoff to white and lower values to black.
  Threshold 1-255 defaults to 128; higher makes the image darker.
* Ordered dithering uses a repeating 4x4 pattern to simulate gray.
* Blue noise uses a less regular pattern.

The cutoff applies only to Threshold. Save, then reboot for rendering/conversion
changes. Uploaded-font rasterization is separate from image conversion.
Use [font guidance](../media/fonts.md) for legibility.

## Balance freshness and panel work

Passive binding interval 100-60000 ms controls idle data checks.
Minimum presentation interval 250-60000 ms bounds physical refresh frequency.
Higher values combine rapid changes and save panel work.
Refresh clock values on minute boundary keeps visible time bindings current
between passive checks.

Full-refresh threshold 0-65535 schedules a full waveform after that many regional
updates; 0 disables scheduled full refresh. Some grayscale implementations always
use full-panel waveforms, so consult [panel notes](../hardware.md).
These refresh settings apply live after Save.

## Configure panel VCOM

The **VCOM (advanced)** section is always visible in E-paper Presentation.
Only drivers that consume configured VCOM use it; other drivers ignore the
saved value. This setting does not program calibration EEPROM.

Use the negative voltage specified on your panel, such as `-1.530` V.
The input accepts values from `-5` to `0` V in `0.001` V steps.
An incorrect voltage can degrade image quality; do not copy another panel's value.
Choose **Save**, then reboot to apply the change.

Leave the value at `0`, or choose **Use board default**, to follow the default
shown for the current firmware. Zero is a selection, never an applied voltage.
If you later install firmware for another panel/driver, the same saved zero
uses that firmware's board default. An explicit nonzero value is preserved,
so check it when changing panels. If no board default is specified and the
driver requires VCOM, enter the panel's voltage before restarting.

## Clear ghosting and verify

Choose **Full refresh now** to request a full update, or add a supported
Display refresh action to a pad.
Use static labels, useful fallback text, and modest update rates.
Avoid animations or second-by-second changes where minute-level information
is enough.

Check the physical panel, not only the portal preview/framebuffer.
If content is correct in a screenshot but stale on glass, inspect presentation
cadence and request a full refresh before changing bindings.

[Choose e-paper](epaper.md) | [Specialized workflows](README.md) | [User guides](../README.md)
