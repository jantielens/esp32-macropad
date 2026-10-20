---
title: Mousepad
description: Configure a relative USB pointer surface, button zones, two-finger scrolling, and reliable exit controls.
---

A Mousepad turns a button's touch area into a relative pointer surface for a
USB-connected host. It supports pointer movement and clicks, with dragging or
two-finger scrolling available when the touch hardware can track the needed
contacts.

## Prepare USB and touch

Select [USB HID transport](../integrations/keyboard-mouse-gamepad.md), save,
reboot, and connect the native USB data port. BLE/Off do not output mouse reports.
In Button Content, select Mousepad and give it a usable span.
Single-contact touch supports pointer movement and zone clicks, not simultaneous
hold-and-move dragging. Check contact capability in the [hardware reference](../hardware.md).

## Tune the pointer

* Movement threshold 0-12 px, default 3, suppresses small motion before dragging
  or scrolling. Zero removes the dead zone but makes accidental movement easier.
* Sensitivity 0.1-5, default 1, scales movement and two-finger scrolling.
* Acceleration 0-5, default 0, amplifies fast pointer motion, not scrolling.

Slide to move relatively; lift/reposition without jumping to an absolute location.
A stationary tap up to 250 ms sends left click. Crossing the movement threshold
cancels tap eligibility even if you return. Two short taps double-click.
Movement after a tap does not hold a mouse button.

## Hold a mouse button

Enable Mouse buttons for left/right zones in the bottom 20%.
A zone presses immediately on touch and releases on lift.
With multiple contacts, hold a zone while another finger moves above it to drag.
The moving finger can lift/reposition without releasing the held button.
Roles stay fixed at touchdown; crossing a boundary does not change them.
Only one mouse button can be held at once.

For middle clicks or a separate right-click control, use an ordinary
[Mouse button action](../integrations/keyboard-mouse-gamepad.md#mouse-and-gamepad-actions).
It does not turn a click into dragging.

## Scroll and exit

Two fingers beginning above the button strip scroll by their midpoint.
The second finger suppresses pointer/tap behavior. After threshold crossing,
scroll locks to the dominant axis, vertical on a tie.
Lifting either finger stops the gesture; lift all before another.
Additional fingers do not replace captured contacts.

Reverse scroll direction flips scrolling; Inertia 0-5 enables optional coasting.
At sensitivity 1, 20 midpoint pixels produce one wheel step.
See [Scrollpad](scrollpad.md) for coasting limits and cancellation.

Show Back button adds a top-left navigation target, default 44 px, configurable
1-512 px and clamped to content size. Enable it separately from its size.
A short stationary tap goes back; motion over 8 px or hold cancels it.
It works without USB but needs screen history.
Provide a separate exit if history can be empty.

Save Pad and test pointer, click, drag, and exit on your host.

## Understand ownership

Mousepad contacts remain captured outside its bounds. They do not dispatch
ordinary actions/holds/swipes. Disable pad swipe actions disables all swipes
everywhere on that pad, even when USB is disconnected.
Only one mouse surface owns input at a time; independent gamepad widgets can
coexist when the hardware provides enough contacts.

Pad replacement, hiding/removal, wake suppression, USB reset, and OTA cancel
input and release ownership. Lift every finger before restarting.
There is no pinch zoom, two-finger right-click, or three-finger gesture.

[Widget chooser](README.md) | [User guides](../README.md)
