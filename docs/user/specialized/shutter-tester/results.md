---
title: Interpret shutter results
description: Separate exposure accuracy, within-shot uniformity, repeatability, and geometry-dependent curtain diagnostics.
---

Shutter results describe exposure accuracy, variation between sensor positions,
and consistency across repeated shots. Those are different measurements, so a
single pass badge is not a complete assessment of shutter behavior.

First confirm [calibration, illumination, and intended target](README.md).
A result is optical evidence at the measured sensor locations, not a general
camera certification.

## Exposure accuracy and verdict

Duration averages valid sensors.
Positive deviation means longer exposure/slower than target; negative means
shorter/faster. Stops expresses the measured/nominal ratio logarithmically.

Pass/warning/fail is driven only by absolute exposure deviation:
within approximately one-third stop passes; over one-third warns; over one-half
fails. Capping, sensor spread, and repeatability have separate diagnostic
colors but do not drive the verdict badge.
An unlocked nearest-speed match is not proof the camera's intended setting
was used.

## Distinguish two spreads

Sensor spread is variation among sensors in one shot.
Shot spread is peak-to-peak variation across repeated shots at one target.
Repeatability is population coefficient of variation across at least three
shots; one shot cannot establish it.
Compare repeated shots at the same locked nominal setting.

In the deviation scatter plot, zero is nominal.
Negative points are fast, positive slow. Repeated-shot average diamonds and a
speed trend summarize the series, not independent calibration evidence.
Inspect individual failures rather than relying only on an average.

## Interpret capping and four-corner geometry

Capping gradient expresses exposure variation in stops/mm.
It requires multiple valid channels and measured nonzero sensor spacing.
The full-frame capping number extrapolates to a 36x24 mm frame: the four-corner
case combines width/height estimates, while the line-array case uses its diagonal.
It is not a direct measurement of every point across that frame.

Four-corner mode can separate X/Y gradients and estimate curtain skew/twist.
Wrong channel order, offsets, uneven light, and channel timing differences can
imitate mechanical effects. Repeated discrete skew at every speed can point
to setup/sampling offsets.
Missing geometry/data means unavailable, not zero capping.

## Read waveform and curtain details

Pulse width is the measured exposure; the ideal window is nominal duration.
A wider actual pulse means longer exposure.
Per-sensor curtain edge timings describe optical transitions at that sensor,
not full-gate curtain travel time.
Some captures expose diagnostic edge durations while suppressing a physically
invalid balance ratio, such as full-open or recovery-tail regimes.

The curtain simulation and sweep orientation are illustrative.
Flip/sweep controls change the visualization, not saved measurements or verdict.
Use [display controls](display.md) and repeated clean captures before attributing
an unusual shape to a specific fault.

[Sessions/export](sessions.md) | [Shutter setup](README.md) | [User guides](../../README.md)
