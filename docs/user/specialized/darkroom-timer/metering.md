---
title: Calibrate and read the darkroom meter
description: Establish reference exposure, estimate grade/time from bright and dark readings, and compensate for enlargement.
---

The darkroom meter compares light readings at the paper plane to suggest
contrast grade and exposure time. A processed-paper reference connects those
readings to your actual printing setup; measurements alone do not replace
that calibration or a test print.

Use the [timer/relay setup](README.md) first and follow the
[board's sensor placement](../../hardware.md).
Reads are requested through Meter actions, not continuous automatic exposure.
Keep paper, chemistry, filter, lens aperture, and enlarger configuration fixed
while establishing a reference.

## Establish a reference

1. Make a processed/dried test strip spanning a useful exposure range.
   For example, use half-stop steps around 8 seconds, extending roughly 2-16.
2. Choose the patch matching Zone V/middle gray, not the first barely visible
   patch or solid black.
3. Set that exposure as Zone V time.
4. Remove the negative and read bare-bulb Lref at the paper plane under the same
   aperture/filter/height conditions.
5. Repeat/check a known negative before relying on the recommendation.

Recalibrate when paper, chemistry, filter, aperture, or enlarger geometry changes.
Grade estimates from a bright/dark ratio do not themselves require Zone V,
but the exposure-time recommendation does.

## Read a negative

With the negative in place, read its clearest/brightest useful area and its
densest/darkest useful area at the paper plane.
Check L bright, L dark, subject brightness range, and suggested grade.

Recommended time needs positive Zone V, Lref, bright, and dark readings.
It scales the reference by Lref divided by the geometric mean of the two
negative readings. Missing calibration displays unavailable rather than a
trustworthy guessed time.
Make a test print; subject interpretation and processing still matter.

## Compensate for enlargement

Measure Magnification A before changing height and Magnification B afterward,
using the same unobstructed reference/aperture/filter conditions.
The compensation factor is light A divided by light B.
The suggested compensated time scales the current exposure by that factor.
For a dimmer enlarged projection, it therefore grows rather than shrinks.

Use Clear magnification compensation to discard the pair when it no longer
matches the current setup.
Do not mix a different filter/aperture change into a height-only comparison.
If readings fail or remain unavailable, inspect sensor/power and
[diagnostic logs](../../device/diagnostics.md) before replacing calibration.

[Darkroom Timer](README.md) | [User guides](../../README.md)
