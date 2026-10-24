---
title: Choose a widget
description: Select a visualization or interaction mode for a button and follow its focused configuration guide.
---

A widget gives a button a specialized display or interaction mode, such as a
gauge, history chart, list, or touch controller. Choose one for the kind of
information you want to show or the input you want to send.

Choose a widget in Button **Content**, configure it, then **Save Pad** and
inspect the device. Optional sources/styles can remain saved when collapsed;
collapsing a section does not disable it.

## Interact

* [Mousepad](mousepad.md) for pointer movement and supported multi-contact gestures
* [Scrollpad](scrollpad.md) for one-finger wheel scrolling
* [Gamepad](gamepad.md) for sticks and held controls
* [Rocker and Numeric Rocker](rockers.md) for adjustments
* [List](list.md) for provider items and selection actions
* [Toggle switch](#toggle-switch) for local state or a reported on/off category

## Visualize

* [Bar Chart](bar-chart.md) for one to four numeric sources
* [Gauge](gauge.md) for rings, signed balance, needle, and targets
* [Sparkline](sparkline.md) for sampled history
* [Table](table.md) for structured rows
* [Camera Preview](../media/camera.md#put-a-preview-on-a-pad) for an attached camera
* [Extension](../media/extensions.md) for installed native content
* [Shutter Waveform](../specialized/shutter-tester/display.md) for optical captures

Ordinary buttons can instead use [labels/icons](../pads/appearance.md) or
[background images](../media/images.md).
Full-screen tap actions take precedence over widgets; touch-owning widgets
can suppress ordinary tap/hold/swipe behavior.

## Toggle switch

A Toggle switch shows an on/off category inside a button. With a state source,
the reported value is authoritative: tapping requests the opposite category
through actions, rather than assuming the command succeeded. Without a source,
it is a local switch that starts off and resets off whenever you leave the pad,
reload its configuration, or restart the device. No state is written to storage.

1. Select a button in **Pads**, then choose **Toggle switch** in **Content**.
2. Leave **State source** blank for local mode, or enter a binding such as
 `[ha:switch.desk_lamp]`. A literal source such as `on` is also supported;
 it behaves as a fixed reported value, not local state.
3. Set **On value** and **Off value**, normally `on` and `off`. Matching ignores
 surrounding ASCII whitespace and letter case. Other values are unavailable;
 there are no implicit `true` or `1` aliases.
4. Configure **Turn-on actions** and **Turn-off actions** under **Actions**.
 For a lamp, use explicit HA `turn_on` and `turn_off` services, not `toggle`.
 An empty list is allowed; a bound switch with no selected actions does nothing.
5. Choose track/knob colors and **Switch size (%)** (20-100%, default 100%).
 Choose **Icon Position**: Left, Above, or Below. **Read-only** follows the source
 but ignores taps. State source has the standard binding-builder and reference buttons.
6. Select **Save Pad**. No reboot is needed. Tap the switch or button background
 on the device; holding does not run a separate long-press action.

While a bound request is pending, the switch stays in its reported position,
dims, and ignores further taps. Confirmation in the requested category clears
pending; after five seconds without confirmation it stops waiting without
retrying. Unavailable data dims the switch, blocks input, and retains its last
known position. Before the first valid report, its knob is hidden. Recovery
adopts the current value without sending actions. Read-only alone does not dim it.

Existing labels, fonts, foreground colors, and the icon remain available.
Top and bottom labels reserve their own rows. Within the remaining area, the
center label and switch share a row, with the label on the left. With no labels,
the switch fills the available area as far as its 2:1 aspect ratio permits.
An icon defaults to the left of that row; Above and Below place it around the
row, inside the outer labels. Switch size scales the largest fitted switch,
not the entire button. Existing saved size percentages remain valid; older
top/bottom switch placement is ignored.

Use Content Padding and Icon Scale to adjust spacing and icon size. UI Offset
moves the middle group together; label-style offsets nudge the center label.
Binding updates recalculate the fit as text changes. Icon scaling and label
font upscaling are included in the fit. When less than four pixels
of switch width remain, it is hidden instead of overlapping content. Enlarge
the button or reduce its content if needed. Explicit offsets and oversized text
are clipped at the button boundary. The pad editor shows button styling and
labels, with a large subdued symbol identifying each widget behind its labels.
It does not reproduce widget graphics or widget-specific visual settings.
Binding text is abbreviated to fit; hover over a binding or select the button
to read its full text. The grid does not resolve bindings, show sample or live
values, or dispatch actions.

### Bureau comfort setpoint

For `climate.bureau_boven_thermostaat`, keep the existing **Turn-on actions**
calling `set_temperature` with `{"temperature": 21}` and **Turn-off actions**
with `{"temperature": 10}`. This is a comfort-setpoint category, not thermostat
power or current heating activity. Keep the top label `Bureau`, thermostat icon,
and existing dynamic foreground styling. Keep the bottom label binding:

```text
[ha:climate.bureau_boven_thermostaat;attributes.temperature;%.1f °C]
```

Use this **State source**, with on/off comparison values `on` and `off`:

```text
[expr:[ha:climate.bureau_boven_thermostaat;attributes.temperature]>20?"on":"off"]
```

Targets 21 and 22 are on; 20, 19, and 10 are off. A reported target of 22
confirms an on request even though its action requested 21. Any valid target
at or below 20 confirms off. The bottom label continues to show the actual
temperature. HA changes update the switch without executing either action list.

Do not add an `off` or numeric fallback: disconnected, missing, empty, unknown,
unavailable, or nonnumeric inputs must leave this expression unavailable, not
classify as off. Explicit fallbacks inside expressions can replace failed inputs
with valid values and hide the failure. The toggle rejects unresolved direct
bindings even when they have display fallbacks, but cannot recover a failure
that an inner expression has already converted into a valid result.

Set up the connection through the [Home Assistant guide](../integrations/home-assistant.md).
Local mode and other binding schemes do not require HA support.

[Pad tasks](../pads/README.md) | [User guides](../README.md)
