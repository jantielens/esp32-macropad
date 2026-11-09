---
title: Rocker and Numeric Rocker
description: Configure two action zones or fine/coarse numeric steps with a dedicated adjustment template.
---

Rockers divide a button into directional control zones for related actions.
A Rocker provides two action zones; a Numeric Rocker adds fine and coarse
signed steps plus a center action. Use them for adjustments such as brightness,
volume, or a countdown value.

## Arrange labels and icon

Labels and the icon appear between the directional controls by default.
Choose **Labels and icon placement > Use widget layout** to use **Widget, labels
and icon layout** instead, including its overlaid positions.
For centered or beside-widget layouts, **Labels and icon size %** uses the
rocker axis: 0 measures the content automatically, while 10-90 requests a
percentage of the widget span. Overlaid positions ignore this setting.
Centered content is capped to the space between the Rocker indicators or the
Numeric Rocker center zone. This does not change tap-zone sizes.

The group is not a separate control. Taps on it use the touched zone: Rocker
still divides at the midpoint, and Numeric Rocker's center runs its center
actions. Visual offsets can move content into another zone; the touch position
determines the action. On small buttons content can shrink or be clipped.
Enlarge the button or reduce the content if needed. The portal overview shows a
directional symbol, not the actual layout; check the device after saving.

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
* Notification: message `Adjusted by {step}`

Small/large zones substitute the signed adjustment into supported text fields,
including notification messages, variable values, MQTT topics and payloads,
keys, volume, brightness, and timer values. `{step}` is the change requested,
not the resulting absolute value. For a decrement of 10, `Adjusted by {step}`
becomes `Adjusted by -10`.

Event-token support is separate from live bindings. Commands, variable names,
action field names, and native numeric properties such as delay duration stay
literal. The editor rejects unsupported fields and substitutions that exceed
their byte capacity. `{value}` belongs to sliders and is unavailable here.

The center has its own up-to-three tap actions; the adjustment action may be
empty. The editor omits ordinary long-press actions for this widget.
Inner zones use tap feedback, outer zones use hold feedback unless the action
produces its own sound.

Indicator colors are literal, not live expressions.
Save Pad and test negative/positive small/large steps, the center, and limits.
Timer/Alarm adjustments require whole seconds/minutes even if the rocker
permits fractional steps.

[Timer behavior](../device/timers.md) | [Widget chooser](README.md) | [User guides](../README.md)
