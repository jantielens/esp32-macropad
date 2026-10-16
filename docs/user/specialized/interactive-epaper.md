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
