---
title: Gauge
description: Configure numeric rings, signed balance, arc geometry, needle and ticks, and shared targets.
---

A Gauge shows numeric readings as arcs around a shared scale, optionally with
a needle or target marker. Use separate rings to compare readings, or pair
two sources to show their positive and negative contributions.

## Configure a ring

Choose Gauge in Button Content and enter a numeric Ring 1 binding.
Set Min/Max with minimum below maximum, Arc span 10-360 degrees, and Start angle.
Angles use 0 at 3 o'clock, 90 at 6, 180 at 9, and 270 at 12.
Save Pad and verify low/mid/high values on the device.
Center label/icon sits near the pivot; top/bottom labels provide context.

For a seconds clock, use `[time:%S]`, Min 0/Max 60, span 359, start 270, and
center label `[time:%H:%M]`.
For temperature, choose a realistic scale rather than formatting units into
the input.

## Add rings or balance a pair

Bindings 2-4 add concentric rings with their own colors/start labels.
They share range and arc geometry.
Zero-centered fill applies signed values from zero on unpaired rings.

Pair rings 1/2 combines slot 1 in the positive direction and slot 2 in the
negative direction. Pair 3/4 works independently.
Use a scale containing zero. A missing/invalid partner is treated as zero,
which is not evidence the physical quantity is zero.
Pair 1 drives its needle using slot1 minus slot2.

For a power balance, pair positive house consumption with positive solar
production, then use a third unpaired signed grid source.
See [home energy](../examples/home-energy.md).

## Tune readability

Arc thickness is 5-50% of radius. Multiple rings have automatic gaps.
Needle applies to the outermost active ring; width 0 hides it.
Cutoff 0-99% removes the inner portion to avoid labels.
Tick count creates interior divisions; N ticks divide into N+1 segments.
Animation 0-5000 ms applies to arcs/needle, with the initial value immediate.

Track Outline Width 0-8 px follows the full track. Gauge fills are not patterned.
Tick, needle, track, and arc colors can use expressions where supported.
Target Value marks every active ring; empty disables it while 0 is configured.
Target Zone Angle is total width, 0-90 degrees, centered on the target.
Target tick width 0 hides that tick.

Collapsed/off controls remain saved and validate; collapsing a ring does not
remove its binding. Save and inspect the physical result.

[Widget chooser](README.md) | [User guides](../README.md)
