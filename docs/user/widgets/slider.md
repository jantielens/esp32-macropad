---
title: Slider widget
description: Configure a touch slider for numeric controls, live values, and release actions.
---

A Slider lets you set a numeric value by dragging or tapping its track. It is
available on display builds with touch input. A slider can keep a local value or
follow a numeric binding, and optionally run up to three actions when you release
after changing the selection.

## Configure a slider

1. Select a button in **Pads**, then choose **Slider** in **Content**.
2. Set **Minimum**, **Maximum**, and **Step**. The endpoints must be finite,
   within -1,000,000 to 1,000,000, and the step must divide the range exactly.
   The range supports up to 10,000 steps and four decimal places.
3. Choose **Horizontal** or **Vertical**. Horizontal values increase to the
   right; vertical values increase upward. **Control length (%)** reserves a
   percentage of the content width for a horizontal slider, or the height
   between labels for a vertical slider, when a center label or side icon is
   present. Without either, the slider uses the full available length. The
   length includes the knob and does not change when label text changes.
   Track and knob sizes of `0` use automatic sizing: the knob targets 50 px,
   limited to 60% of the available thickness and 25% of the slider length.
   The automatic track is 40% of the knob diameter, normally 20 px for a
   50 px knob. Explicit sizes override automatic sizing and are limited by
   the available thickness.
4. Choose literal track, fill, and knob colors. Existing top and bottom labels
   remain in their reserved rows. The icon and center label sit to the left of a
   horizontal slider or beside a vertical slider. **Icon position** can also
   place the icon above or below the row. Use an existing label binding to
   display a reported value. Increase the button size or reduce control length
   when more space is needed for its icon and label.
5. Leave **Data binding** empty for a local control, or enter a binding such
   as `[mqtt:home/level]` or `[ha:media_player.living_room;attributes.volume_level]`.
   A bound slider stays unavailable until it receives a valid numeric value.
6. Optionally configure up to three **Value set actions** under **Actions**.
   All actions available on the device can be selected, including navigation
   and delays. Use `{value}` in a supported text field when the action needs
   the selected number; the token is optional. The list runs once on a changed
   release. Without actions, a local slider keeps its value and a bound slider
   follows its source. Ordinary tap and long-press actions are not available.
7. Select **Save Pad**. No reboot is needed.

Use Content Padding and UI Offset to adjust the group. If the button is too
small to fit a usable control, the slider is shown unavailable instead of
overlapping the button's labels.

## Use value actions

`{value}` is replaced with the selected absolute number, formatted without
exponent notation or unnecessary trailing zeroes. Supported text fields include
notification messages, variable values, MQTT topics and payloads, Home Assistant
entity IDs, service names and data, volume and brightness values, timer values,
and key sequences. Tokens can appear inside surrounding text or quoted JSON
strings. Fixed commands, variable names, action field names, and native numeric
properties such as a delay's duration do not accept tokens.

* To set device volume, select **Volume**, choose **Set volume**, and enter
   `{value}` in **Value (%)**. Use a range from 0 to 100.
* To set brightness, select **Brightness**, choose **Set brightness**, and enter
   `{value}` in **Value (%)**. Keep the minimum at or above the device's minimum
   user brightness, normally 5.
* To publish a numeric MQTT command, select **Publish MQTT message**, enter a
   topic such as `home/level/set`, and enter `{value}` as the payload.
* To report the selection, use a notification message
   `Brightness set to {value}%`. A selection of 20 displays
   `Brightness set to 20%`.
* To save the selection, set variable `a` to `{value}` for text `20`, or
   `{value}%` for text `20%`. The variable name stays fixed.
* To set a Home Assistant light, choose **Call Home Assistant service**, use
   entity `light.kitchen`, service `turn_on`, and data `{"brightness":{value}}`.
   Set the range to 0 through 255.
* For media volume, use entity `media_player.living_room`, service `volume_set`,
   and data `{"volume_level":{value}}`. Set the range to 0 through 1 with step
   `0.01`.

Every action in the list receives the same release-time value, even when a
delay or confirmation pauses the list. `{step}` remains the numeric rocker's
signed adjustment token, not the slider's absolute selection. The editor rejects
the wrong token, unsupported fields, and substitutions that exceed a field's
byte capacity. Event tokens are unavailable in ordinary button actions.

Integer-only actions still require whole numbers after substitution. Use a
whole-number range and step for alarm minutes. Home Assistant data must expand
to a valid JSON object: `{"brightness":{value}}` preserves the selected number,
whereas adding numeric suffixes can make fractional selections invalid. An
invalid expanded action is rejected before sending a request and stops the
remaining actions in that list.

## Source and feedback behavior

A valid source value is clamped to the range and snapped to the nearest step;
ties snap upward. A valid `0` is a value. Empty, partially numeric,
non-finite, and over-precision source values are unavailable. Source updates
never dispatch an action and do not move the slider during an active drag.

On a changed release, a bound slider displays the requested value while it waits
up to five seconds for a matching reported value. A different report is kept
for reconciliation after the wait ends. An accepted action is not proof that a
remote device changed. A matching report clears the pending state; timeout
shows the latest report. A local slider keeps its selection for the current
instance only.

Press loss, input reset, hidden or disabled state, overlays, and leaving the pad
cancel an active drag without sending an action. A drag that starts on the
slider cannot navigate to another pad.

[Widget chooser](README.md) | [Bindings](../pads/bindings.md) | [Home Assistant](../integrations/home-assistant.md)
