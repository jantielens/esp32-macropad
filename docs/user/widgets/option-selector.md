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
5. Under **Actions**, configure the shared **Option action** list. For a typed
   operation, select **Dynamic** beside an argument and use `[event:value]`
   to read the tapped option's value.
6. Adjust the colors and layout if needed, then select **Save Pad**. No reboot
   is needed. Test the options on the device and check the receiving service
   when actions control something external.

The option action list replaces ordinary button tap actions; long-press actions
are not available. `[event:value]` is the option's value, not its label.
An option without a value supplies an empty string. An action that requires a
value may be rejected during validation or fail when run.

### Control Wi-Fi station mode

Keep a local recovery method available before testing: enabling station mode
can replace the active AP, and disabling station mode can disconnect your
portal session.

1. Add options with values `aan` and `uit`. Set **Selection mode** to **Bound**.
2. Set **State source** to
   `[expr:[health:wifi_station_enabled]=="ON"?"aan":"uit"]`.
3. Add **Set Wi-Fi station mode** to the option actions. Enable **Dynamic** for
   **Station mode** and enter
   `[expr:[event:value]=="aan"?"true":[event:value]=="uit"?"false":"invalid"]`.
4. Save the pad and test both options. An unknown value fails the action,
   displays an error, and stops its remaining actions.

The state source reports applied station mode, refreshed at most every two
seconds; it is not connection status. During a pending or rejected request it
can still show the previous applied state. A successful action means the
network request was admitted, not that station mode was applied or connected.
Enabling station mode may replace an active AP. Disabling station mode leaves
an active AP alone. Turning off the entire radio is a separate command.

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

Under each option's **Style overrides (optional)**, **Foreground** affects its
label or monochrome icon, and
**Background** colors its fill. Individual option colors override the shared
and selected colors. Reset a color to its automatic default with the reset
icon. Group and option background colors, individual colors, and labels can
use [bindings](../pads/bindings.md); selected colors are literal colors.

Under **Option selector layout**, use **Selector height (%)** to reduce the
height of the centered group and options. The default is 100%; touch targets
remain full height. Small heights can clip labels or icons.
Set **Option inset** to add space around
each visible option without shrinking its touch target. It defaults to 4 pixels;
use 0 for no inset. **Label font size** changes option text; 0 inherits the
button's center-label font size. **Icon size (%)** scales icons within the
available area, with 0 fitting the area. **Icon badge padding** adds space
around an icon and reduces its available size.

### Set widths and reserve empty space

Each option has an optional **Weight** under **Style overrides (optional)**, from 1
to 1000 using whole numbers.
The selector divides its available width in proportion to these weights.
Leave Weight blank for a label or icon to use 1. Weights of 1, 1, and 2 give
the first two options a quarter of the width each and the last option half.
Equal weights give equal widths.

An option without a label or icon is ignored unless you set its Weight.
Setting a weight on an empty option creates an invisible spacer. It has no
individual background, touch target, value, or selection. The group background,
if enabled, still covers the spacer.

For a status row toward the right, leave Option 1 empty and set its Weight to 7.
Add Wi-Fi, signal, and battery labels or icons in Options 2-4, leaving their
weights blank. The spacer takes 70% of the width and each indicator takes 10%.
Use a trailing spacer to push content left, or matching spacers at both ends
to center it. Spacers count toward the five-option limit.

Clear a spacer's Weight to stop reserving space. To turn an existing option
into a spacer, clear its label or icon and its Value, and keep an explicit
weight. A bound label that currently resolves to empty remains a normal option.

Widths are proportional, not fitted to content. Large weight differences can
make an option too narrow to read or tap; test the result on your device.

### Hide or disable an option

Each option also has an optional **State** under **Style overrides (optional)**:
`enabled`, `disabled`, `hidden`,
or a binding that resolves to one of these. Use it when an individual choice
should be unavailable or hidden independently of the selector's reported
selection. Leave it empty for an enabled option.
Hidden options keep their allocated space rather than moving neighboring
options.

## Keep a temporary local choice

Use Local mode when the highlight only needs to last while the button is
visible. For example, create options **Red**, **Green**, and **Blue** without
values. Choose **Local**, leave **Option action** empty, and save. Tapping an
option highlights it; leaving and returning to the pad clears the highlight.

Add values `red`, `green`, and `blue` and an action if the choice should do
something. For example, add **Show notification**, enable **Dynamic** for
**Message**, and enter `Selected [event:value]`. Tapping **Green** displays
`Selected green`.
The highlight alone does not store a setting or control a light.

## Share a choice through a runtime variable

Use Bound mode with a runtime variable when other buttons or pads should read
the choice. Configure options **A**, **B**, and **C** with values `A`, `B`,
and `C`. Set **State source** to:

```text
[var:a|unset]
```

Add a **Set variable** option action with **Name** `a`. Enable **Dynamic** for
**Value** and enter `[event:value]`. Save the pad and tap **B**: the action
writes `B`, and the reported variable selects **B**. Another button can show the
choice with `[var:a]`.

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

Add a **Publish MQTT message** option action with topic `home/fan/set`. Enable
**Dynamic** for **Payload** and enter `{"mode":"[event:value]"}`. Tapping **High** sends
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
