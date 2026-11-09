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
* [Slider](slider.md) for touch-based numeric value setting
* [Option selector](option-selector.md) for choosing one of several named values
* [Rocker and Numeric Rocker](rockers.md) for adjustments
* [List](list.md) for provider items and selection actions
* [Toggle switch](#toggle-switch) for local state or a reported on/off category

## Visualize

* [Face](#face) for an animated companion that reacts to emotion names
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
All widgets use the shared [labels and icon layout](../pads/appearance.md#arrange-button-content).
Touch widgets keep their own input behavior.

## Face

A Face adds personality to a button rather than showing a measurement. On
animated LCD/OLED displays, it blinks, looks around at irregular intervals, and
transitions smoothly between emotions. Interactive e-paper displays also allow
face motion, with a roughly one-second closed-eye hold instead of a quick blink.
Updates still follow the device's binding and e-paper refresh settings, so gaze
and emotion changes can appear as discrete steps. Long refresh or binding
intervals can skip poses, including the closed-eye pose. The option appears only
when your installed firmware includes it; E-Paper Frame firmware does not offer
pad widgets.

1. Select a button in **Pads**, then choose **Face** in **Content**.
2. Choose **Style**: **Robot Eyes** for an eyes-only face, or **Doodle Buddy**
 for eyes, eyebrows, and a mouth.
3. Set **Emotion** to `content`, `relaxed`, `happy`, `worried`, `angry`, `sad`,
 `surprised`, or `sleepy`. You can choose a suggested value or enter a binding
 that returns one of these lowercase names. Blank means `content`.
4. Choose **Liveliness**: **Still**, **Calm**, **Normal**, or **Lively**.
 **Still** disables blinking and gaze movement, keeps the gaze centered, and
 only animates emotion changes. The other options change idle activity, not the
 emotion source. **Normal** is the default.
5. Use the existing button text color for the face and background color for its
 background. Labels and icons remain available in the shared content group.
 Ordinary tap and long-press actions still work.
6. Select **Save Pad**. No reboot is needed. Inspect the face on the device;
 the editor grid shows the widget symbol, not a live animation.

For example, to react to power consumption from MQTT, enter this in **Emotion**:

```text
[expr:[mqtt:home/power]>2000?"worried":"relaxed"]
```

Values above 2000 produce `worried`; 2000 or lower produces `relaxed`. This simple
example has no hysteresis or duration tracking. To distinguish a brief spike
from sustained high consumption, calculate a named emotion in your automation
system and bind the face to that result.

Missing or unrecognized results transition to `content`, with a device log
warning once per invalid period. A valid result recovers automatically. Repeated
values do not restart the transition. Standard
[binding fallbacks](../pads/bindings.md) can explicitly choose another supported
emotion when a source is unavailable.

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
5. Choose track/knob colors and **Switch width (%)** (20-100%, default 50%).
 **Read-only** follows the source
 but ignores taps. State source has the standard binding-builder and reference buttons.
6. Select **Save Pad**. No reboot is needed. Tap the switch or button background
 on the device; holding does not run a separate long-press action.

While a bound request is pending, the switch stays in its reported position,
dims, and ignores further taps. Confirmation in the requested category clears
pending; after five seconds without confirmation it stops waiting without
retrying. Unavailable data dims the switch, blocks input, and retains its last
known position. Before the first valid report, its knob is hidden. Recovery
adopts the current value without sending actions. Read-only alone does not dim it.

Switch width is a percentage of the assigned widget region, capped by the
available height at a 2:1 aspect ratio. When less than four pixels of switch
width fit, the switch is hidden. Enlarge the button or reduce its content if
needed. The pad editor shows labels and a large symbol identifying the widget
in a consistent overview.
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
