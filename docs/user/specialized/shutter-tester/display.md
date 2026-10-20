---
title: Configure the shutter display
description: Use waveform widgets, capture feedback, binding labels, and visualization-only controls.
---

The shutter display combines captured waveforms with labels and instrument
controls on a pad. A waveform shows the optical pulse shape, while bindings
and actions provide measurement feedback and capture/session control.

## Put a waveform on a pad

Choose Shutter Waveform in Button Content.
Select all channels or a specific available sensor and a usable button span.
Configure line colors/width, optional threshold/trigger/target overlays,
and Y inversion. Save Pad and fire a test shot.
The widget is blank before a capture and automatically zooms around the pulse.

Displayed channels follow the running topology; a four-corner preset needs four
real sensors. Compare the portal's per-sensor chart when a small device widget
hides detail.

## Keep labels useful

Use the live Shutter binding reference for duration, target, deviation, verdict,
session state, sensor values, and geometry-dependent diagnostics.
Prefer named [pad bindings](../../pads/bindings.md) for long expressions.
Do not treat an unavailable single-sensor gradient as a good zero.

Waveform color fields are short (at most 11 characters) and resolve when the
widget is created, not continuously.
A short `[pad:c]` can reference a longer configured color expression, but changing
upstream data does not make that waveform color live.
Normal label bindings follow their own update behavior.

## Separate actions from visualization

Use Shutter actions for Calibrate, Align, target adjustment/lock,
session control, and guided progression.
The editor's current Action reference owns available commands.
For a list-driven guided test, use the selected provider ID as described in
[sessions](sessions.md#write-a-guided-test).

Portal orientation/sweep flipping is a viewing aid and does not change stored
timing, sensor geometry, or verdict.
Use an unobstructed stop/navigation control and avoid full-screen tap actions
that unexpectedly intercept your instrument buttons.

[Shutter setup](README.md) | [Result interpretation](results.md) | [User guides](../../README.md)
