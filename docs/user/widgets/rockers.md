---
title: Rocker and Numeric Rocker
description: Configure two action zones or fine/coarse numeric steps with a dedicated adjustment template.
---

Rockers divide a button into directional control zones for related actions.
A Rocker provides two action zones; a Numeric Rocker adds fine and coarse
signed steps plus a center action. Use them for adjustments such as brightness,
volume, or a countdown value.

## Use two action zones

Choose Rocker in Button Content and Vertical or Horizontal.
The first action list becomes Up/Left, the second Down/Right.
These are tap zones, not ordinary tap versus hold lists; long press is disabled.
Set a literal indicator color and opacity 0-255, default 80.

For brightness, configure Up Adjust `10` and Down Adjust `-10`.
Use `[health:brightness]` in a label, save, and test both directions.
The same pattern works for volume or other supported adjustment commands.

## Use fine and coarse steps

Numeric Rocker has five zones: outer decrement, inner decrement, center,
inner increment, outer increment. Horizontal increments right; Vertical
increments upward. The initial touch position chooses the zone.

Set Small Step (default 1), Large Step (default 10), and Tap Area Scale 50-150%
(default 100). Decimal steps are allowed, but the destination action must
accept the resulting value. Zero disables that zone pair and gives its area
to the center; both zero makes the full content area center.
Give the control a wide/tall span so all targets remain usable.

Configure one **Adjustment Action** using `{step}`, for example:

* Timer: slot 1, Adjust countdown, Value `{step}`
* Volume: Adjust volume, Value `{step}`
* MQTT: payload `{"delta":{step}}` for an automation expecting a numeric delta

Small/large zones substitute the signed value into supported bindable action
fields, including MQTT payload, keys, volume, brightness, timer, and
feature-specific numeric adjustment fields.
Do not assume a literal field or arbitrary JSON key supports substitution.

The center has its own up-to-three tap actions; the adjustment action may be
empty. The editor omits ordinary long-press actions for this widget.
Inner zones use tap feedback, outer zones use hold feedback unless the action
produces its own sound.

Indicator colors are literal, not live expressions.
Save Pad and test negative/positive small/large steps, the center, and limits.
Timer/Alarm adjustments require whole seconds/minutes even if the rocker
permits fractional steps.

[Timer behavior](../device/timers.md) | [Widget chooser](README.md) | [User guides](../README.md)
