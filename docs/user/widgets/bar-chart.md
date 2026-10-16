---
title: Bar Chart
description: Compare numeric sources with a shared scale, signed or paired fills, captions, tracks, and target markers.
---

A Bar Chart compares up to four numeric readings as filled tracks on a shared
scale. Use it for levels, percentages, or quantities you want to compare at
a glance; signed and paired modes can show values on opposite sides of zero.

## Configure a first bar

Select Bar Chart in Button Content.
Under Data sources, give Bar 1 a plain numeric binding, such as `[health:cpu]`.
Set Scale Min 0/Max 100, choose Vertical/Horizontal, then Save Pad and inspect it.
Use a separately formatted label for units.
All active sources need a meaningful shared range with minimum below maximum.

## Add sources and captions

Bindings 2-4 add bars with separate tracks but one scale.
Each has its own color, optional pattern, and bindable caption.
Captions appear below vertical bars or left of horizontal rows.
Use `\n` for multiple lines and Caption area size 1-200 px to reserve space;
0 is automatic. Empty captions reserve no caption strip.

Thickness 1-100% changes each bar within its own column/row.
Animation 0-5000 ms smooths changes; 0 is immediate and the first value snaps.
Set a suitable update cadence for the display rather than expecting a video-rate
meter.

## Display signed or paired values

Zero-Centered grows from zero instead of the minimum. Use a negative minimum
for values such as import/export power.

Dual Binding Bar 1 and 2 combines two sources into a shared track: bar 1 grows
toward the minimum, bar 2 toward the maximum. The 3/4 pair is independent.
Pairing needs the partner binding and a range containing zero.
Do not copy Gauge pairing direction: its first slot is the positive side.

## Style and mark the track

Base/pattern colors use the [shared picker](../pads/appearance.md#add-a-pattern).
Track Corner Radius is 0-64 px, capped to geometry; paired joins stay square.
Track Outline Width 0-8 px outlines the whole track even when empty/full.
Gridlines 0-20 share the scale but stay within each bar's track.

Target Value adds a bindable marker; empty disables it, numeric zero is a target.
Target Zone % shades a percentage of the range around it.
Marker/track/grid colors can bind where fx is shown.
Collapsed/disabled controls retain values and still must validate before Save.

For complete power examples, use [home energy](../examples/home-energy.md).

[Widget chooser](README.md) | [User guides](../README.md)
