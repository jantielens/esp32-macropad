---
title: Option selector
description: Configure labeled or icon choices, send their values to actions, and follow a reported selection.
---

An Option selector puts up to five choices inside one button. Each choice has
a label or icon and can have a separate value that its actions use. Select a fan
mode, send a scene command, or choose a local setting without dedicating a
button to each choice.

The selection mode determines what the highlight means: no persistent
selection, the last local tap, or a value reported by a binding. A Bound
selector requests a change; it does not assume the receiving device accepted it.
On a display without touch input, use Bound mode to show a reported selection.
Changing options requires a pointer input; hardware-button navigation between
individual options is not supported.

## Configure options

1. In **Pads**, select a button and choose **Option selector** in **Content**.
   Give the button enough space for each option to be easy to tap.
2. Under **Options**, open an option and enter a **Label** or choose an icon.
   Each option uses one or the other. A **Value** is required in Bound mode and
   optional in None and Local modes. If supplied, use 1-63 ASCII letters, digits,
   underscores, or hyphens. It can differ from the visible label.
3. Repeat for up to five options. In Local and Bound modes, nonempty values must
   be distinct, ignoring letter case.
4. Under **Option selector**, choose a **Selection mode**. For Bound mode,
   enter a **State source** that reports one of the option values.
5. Under **Actions**, configure the shared **Option action** list. Put
   `{value}` in a supported action text field to use the tapped option's value.
   Every action in the list receives the same value. For example, an MQTT
   payload can be `{"mode":"{value}"}`.
6. Adjust the colors and layout if needed, then select **Save Pad**. No reboot
   is needed. Test the options on the device and check the receiving service
   when actions control something external.

The option action list replaces ordinary button tap actions; long-press actions
are not available. `{value}` works in supported action text fields, not in every
field or native numeric property. It is the option's value, not its label.
An option without a value substitutes an empty string. An action that requires
a value may be rejected during validation or fail when run.
For JSON strings, include the quotes as in the example above. See
[slider value actions](slider.md#use-value-actions) for supported fields.

## Choose a selection mode

* **None** sends the option actions on every tap and does not keep a selection.
  Use it for independent commands, including commands that can be repeated.
* **Local** highlights the last tapped option and runs its actions on every tap.
  It can also be used without actions as a temporary on-screen selection. The
  highlight is not saved and resets when the button is hidden or disabled or
  you leave the pad.
* **Bound** highlights the option matching the **State source**. A tap on a
  different option runs the actions and waits for the source to report that
  value. Tapping the already selected option does nothing. Without option
  actions, a Bound selector follows its source but does not request changes.

An option with actions shows tap feedback. Local mode still changes its
highlight without actions. On devices with audio, accepted actions and Local
selection changes use the configured tap sound unless an action produces audio.

Matching ignores letter case and surrounding ASCII whitespace. A valid source
value that matches no option leaves all options unselected but allows taps.
An unavailable source leaves no option selected and blocks taps. After a
request, options dim and further taps are blocked until the requested value is
reported or five seconds pass. A timeout does not retry the action or prove
that the requested change happened.

## Style the selector

Open **Option selector colors** to set a background behind the whole group,
separate backgrounds behind unselected options, and foreground and background
colors for the selected option. Enable **Group background** or **Option
backgrounds** to show the corresponding fill. A selected option has a
background even when option backgrounds are off.

Under each option, **Foreground** affects its label or monochrome icon, and
**Background** colors its fill. Individual option colors override the shared
and selected colors. Reset a color to its automatic default with the reset
icon. Group and option background colors, individual colors, and labels can
use [bindings](../pads/bindings.md); selected colors are literal colors.

Under **Option selector layout**, set **Option inset** to add space around
each visible option without shrinking its touch target. It defaults to 4 pixels;
use 0 for no inset. **Label font size** changes option text; 0 inherits the
button's center-label font size. **Icon size (%)** scales icons within the
available area, with 0 fitting the area. **Icon badge padding** adds space
around an icon and reduces its available size.

Each option also has an optional **State**: `enabled`, `disabled`, `hidden`,
or a binding that resolves to one of these. Use it when an individual choice
should be unavailable or hidden independently of the selector's reported
selection. Leave it empty for an enabled option.

## Keep a temporary local choice

Use Local mode when the highlight only needs to last while the button is
visible. For example, create options **Red**, **Green**, and **Blue** without
values. Choose **Local**, leave **Option action** empty, and save. Tapping an
option highlights it; leaving and returning to the pad clears the highlight.

Add values `red`, `green`, and `blue` and an action if the choice should do
something. For example, a notification message `Selected {value}` displays
`Selected green` when you tap **Green**.
The highlight alone does not store a setting or control a light.

## Share a choice through a runtime variable

Use Bound mode with a runtime variable when other buttons or pads should read
the choice. Configure options **A**, **B**, and **C** with values `A`, `B`,
and `C`. Set **State source** to:

```text
[var:a|unset]
```

Add a **Set variable** option action with **Name** `a` and **Value** `{value}`.
Save the pad and tap **B**: the action writes `B`, and the reported variable
selects **B**. Another button can show the choice with `[var:a]`.

The `unset` fallback lets you make the first selection before the variable
exists. Choose a fallback different from every option value, ignoring case.
An explicit fallback counts as a valid report even if the source is
unavailable, so it can mask an outage. A fallback matching an option highlights
that option as if it were reported. Runtime variables survive pad changes but
clear on reboot; see [runtime variables](../pads/bindings.md#read-a-runtime-variable).

## Request and follow an MQTT mode

Suppose an automation accepts `low`, `medium`, and `high` in a JSON command
on `home/fan/set` and publishes its actual mode in the `mode` field of JSON
on `home/fan/state`. Configure **Low**, **Medium**, and **High** options with
those values, choose **Bound**, and set **State source** to:

```text
[mqtt:home/fan/state;mode]
```

Add a **Publish MQTT message** option action with topic `home/fan/set` and
payload `{"mode":"{value}"}`. Tapping **High** sends
`{"mode":"high"}`. The selector highlights **High** when the state topic
reports `{"mode":"high"}`, including when the mode changes elsewhere.

Configure MQTT first and adapt topics, payloads, and values to your receiving
automation. The command alone does not update the reported state. Until a
state message is available, the selector blocks taps; retained state can help
after reconnecting. Avoid a fallback if hiding an unavailable source would be
misleading. See [MQTT setup and actions](../integrations/mqtt.md).

For repeatable commands without reported state, use **None** instead. The same
MQTT action can publish the selected value on every tap, but the selector will
not show which mode the remote device is actually using.

[Widget chooser](README.md) | [Bindings](../pads/bindings.md) | [User guides](../README.md)
