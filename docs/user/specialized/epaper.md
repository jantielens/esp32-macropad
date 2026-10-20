---
title: Choose an e-paper workflow
description: Distinguish sleep-first image frames from interactive pad firmware before configuring content and refresh.
---

E-paper keeps an image visible between physical refreshes, making it suitable
for content that changes less frequently. Frame and Interactive firmware use
that display differently: one fetches prepared images and sleeps, while the
other presents interactive pads.

## Display prepared images between sleeps

[E-Paper Frame](epaper-frame/README.md) fetches a prepared image, updates the
panel, and sleeps. Its portal configures image sources, refresh windows,
power budgets, and status. It has no pad editor or live widget UI.
The image server owns rendering clocks, calendars, or other composed content.

## Use buttons, bindings, and widgets

[Interactive E-Paper](interactive-epaper.md) uses the same pad concepts as other
interactive displays, with presentation settings that coalesce physical refresh.
It is not the Frame fetch/sleep pipeline.
Data bindings can update faster than the panel presents them.

## Match the firmware to the hardware

Check your [hardware entry](../hardware.md) for supported firmware targets,
panel geometry, color/gray formats, power controls, and touch capability.
Do not assume two devices with e-paper use the same firmware or image format.
Device-specific exceptions belong there, not in generic image/pad instructions.

[Specialized workflows](README.md) | [User guides](../README.md)
