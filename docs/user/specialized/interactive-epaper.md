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
Minimum presentation interval 250-60000 ms controls background presentation cadence.
The interval is measured after the previous presentation completes.
Higher values combine rapid changes and save panel work.
On devices supporting interaction-priority presentation, focus, navigation and
dialog changes bypass background settling and this interval once their frame
is ready. A user-triggered **Set variable** action that changes a value also
resolves the current pad's bindings promptly, without waiting for the passive
interval. Background actions retain passive polling; unchanged values do not
request an immediate update. An active panel refresh still completes first;
rapid visual changes
can combine into the latest frame without dropping their actions. Other devices
retain interval-limited presentation; see [panel notes](../hardware.md).
Refresh clock values on minute boundary rechecks visible time bindings
between passive checks. Panel scheduling and an active refresh can delay
the visible update.

Full-refresh threshold 0-65535 schedules a full refresh after that many successful
partial updates. Depending on the driver, a partial update may cover a region or
use a partial waveform across the panel. Set 0 to disable count-based cleanup,
not startup, recovery or manual full refreshes. Modes that always use full
refreshes ignore this threshold; consult [panel notes](../hardware.md).
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

For a [Face widget](../widgets/README.md#face), choose **Still** under
**Liveliness** to stop autonomous blinking and gaze movement while keeping emotion
changes. Animated options cause ongoing panel updates and can increase ghosting;
the polling and presentation intervals still limit visible motion.

Check the physical panel, not only the portal preview/framebuffer.
If content is correct in a screenshot but stale on glass, inspect presentation
cadence and request a full refresh before changing bindings.

[Choose e-paper](epaper.md) | [Specialized workflows](README.md) | [User guides](../README.md)
