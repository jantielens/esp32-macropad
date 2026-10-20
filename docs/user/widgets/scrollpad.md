---
title: Scrollpad
description: Configure one-finger USB wheel scrolling, reverse direction, sensitivity, inertia, and cancellation.
---

A Scrollpad turns a button's touch area into a one-finger wheel-scrolling
surface for a USB-connected host. It scrolls along one configured axis without
moving the pointer or clicking, making it useful beside a Mousepad or as a
dedicated scrolling strip.

## Configure the surface

1. Enable [USB transport](../integrations/keyboard-mouse-gamepad.md), save,
   reboot, and connect the native USB port.
2. Select Scrollpad in Button Content.
3. Choose Vertical or Horizontal, sensitivity 0.1-5, and optional Reverse direction.
4. Choose Inertia 0-5, where 0 disables release coasting.
5. Save Pad and test with a scrollable host application.

It is a relative input surface, not a scrollbar showing the host's position.
Use a tall strip beside a Mousepad or a wide strip below it.
Up/right slides normally scroll up/right; Reverse flips that convention.
Perpendicular motion is ignored.

## Tune scrolling

At sensitivity 1, 20 device pixels produce one wheel step.
Fractional travel accumulates during a touch, then is discarded on release.
The host's own settings/application also affect behavior; horizontal wheel
support varies.

With Inertia enabled, release while moving to coast or pause before lifting
to avoid it. Higher settings coast longer, at most three seconds.
A new touch on either mouse surface stops coasting, as do pad hide, USB reset,
and OTA. A mouse-polling stall over 100 ms also stops it.

## Keep navigation available

The contact remains owned until release, including outside the button.
Stationary taps/holds do nothing. It does not click, move the pointer, or
dispatch normal button actions/swipes.
BLE and Off disable output without restoring ordinary actions.

Disable pad swipe actions is pad-wide, not confined to the surface.
Leave a separate navigation control if needed. Clear full-screen tap actions
if they intercept the surface. Labels, icons, and colors remain customizable.

[Mousepad](mousepad.md) | [Widget chooser](README.md) | [User guides](../README.md)
