---
title: Gamepad sticks and held controls
description: Build a USB controller pad with analog sticks, buttons, D-pad, triggers, and automatic release.
---

Gamepad widgets turn a pad into a USB controller with touch-operated sticks,
held buttons, a directional hat, and triggers. The host receives generic HID
controller input, and the widgets manage touch ownership and release.

Use [USB transport](../integrations/keyboard-mouse-gamepad.md), save/reboot,
and connect the native data port. This is generic HID, not XInput.
Test it in the host's controller view before relying on a game mapping.

## Add a joystick

Choose Gamepad Joystick in Button Content. Select Left/Right stick,
Fixed/Floating center, radial dead zone 0-90% (default 10), and optional X/Y inversion.
Right/down are positive without inversion.
Save Pad and test full travel and release centering.

Travel uses the shorter content dimension and clamps radially outside the
button. Floating centers remain within the content region; a square region
has no room to shift the maximized ring. Tiny content areas below 16 px are
inactive. Center label is hidden; top/bottom labels remain.
Two widgets targeting one stick use first-owner-wins until release.

## Add held controls

Choose Gamepad Button, then a button 1-16, hat direction, or left/right trigger.
Touch holds it; release is automatic. This is not an ordinary tap action list
and needs no separate Up action.
Four hat controls compose diagonals; opposing directions cancel independently.
Triggers are binary 255/0, not pressure-sensitive.

For one-shot or explicitly latched controls elsewhere, use the ordinary
[Gamepad action](../integrations/keyboard-mouse-gamepad.md#mouse-and-gamepad-actions).
Its ownership differs from widget holds.

## Check contact limits and cancellation

Each widget captures one contact at touchdown until release/cancellation.
Crossing widgets does not transfer it. Extra fingers on that widget are ignored,
not promoted when its first finger lifts.
Multiple independent widgets require a touch driver/panel exposing enough
contacts; use [board notes](../hardware.md), not a universal contact count.

These widgets consume ordinary tap/hold/swipe handling even when output is
unavailable. Controller interaction blocks ordinary navigation until all
contacts lift. Full-screen tap actions take precedence; clear them.
Disable pad swipe actions suppresses all pad swipes.

Pad exit/replacement, disconnect/reconnect, OTA, hide/disable/destruction, and
detected touch cancellation release holds without replay.
After cancellation lift all contacts before retrying. Failure detection depends
on the touch driver; do not use a held control as a safety mechanism.

[Widget chooser](README.md) | [User guides](../README.md)
