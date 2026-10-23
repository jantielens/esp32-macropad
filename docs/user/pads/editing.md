---
title: Edit and save a pad
description: Use the canvas and inspectors, arrange a grid, manage spans, and distinguish drafts from saved device state.
---

The Pad Editor combines button layout, content, actions, and optional widgets
into a customizable device screen. Edits remain a browser draft until you save
the pad; displaying a pad on the device uses its saved version.

## Build the layout

1. Open **Pads > Pad Editor** and select a pad in the rail.
2. Select the Pad inspector, then **Layout**. Set its name, columns, and rows
   within the limits offered by the device.
3. Select a position on the canvas. Use Button **Content**, **Actions**, and
   **Appearance** to configure it.
4. Select **Save Pad**, correct any highlighted errors, and wait for success.
5. Select **Show on Device** and check the saved result on the device.

The canvas reflects the device's aspect ratio and spacing, not an exact rendering
of fonts, widgets, or live images. Each pad saves independently.
**Show on Device** does not save the draft.

### Customize row and column sizes

Open **Custom Row / Column Sizes** at the bottom of the Pad inspector's **Layout**
tab to set selected tracks in device pixels. Enter comma-separated
`position:size` values, such as `3:50, 5:80` for row heights or `2:100` for
column widths. Positions start at 1. Sizes must be positive whole pixels.

Unlisted tracks, and every track when a field is empty, stay Auto. Auto tracks
share the remaining space after the pad's spacing and insets. Keep at least one
Auto track on each axis. Buttons keep their positions and spans; a span covers
the combined size of its tracks and the gaps between them. Invalid geometry
keeps the last valid canvas preview and prevents saving until corrected.

Button styling and labels update as you edit valid settings. Widget buttons show
a large subdued symbol behind their labels rather than simulated widget graphics;
ordinary buttons retain their configured icons. Long bindings are abbreviated
without substituting sample or live values. Hover over a binding or select its
button to read the full text. The saved binding is unchanged.

## Create a button from a source

The **Button Builder** configures related fields together, so you do not need
to wire labels, bindings, and actions individually. It offers built-in device
values and the integrations supported by your firmware. Reference and manual
binding entry remain available for other sources.
For a single field rather than a complete button, use the
[binding workflow](bindings.md#build-a-token-from-a-source).

1. Select an empty position on the canvas, then choose
   **Build a button from a source ...** at the top right of the canvas.
   Configured buttons and positions occupied by inherited buttons do not offer
   the builder. On mobile, the empty position stays on the canvas; choose
   **Edit manually** below the builder link instead to open its normal inspector.
2. Select **On-device bindings**, **Home Assistant**, or **MQTT** above the search
   or topic input. Muted options marked **Setup needed** remain selectable
   and show a settings link. Unsupported integrations are hidden.
   Configuring an integration is not required to use built-in values.
3. Load the value:
   * For **Home Assistant**, [configure the URL and token](../integrations/home-assistant.md#configure-direct-ha-access)
     if needed. Search for an entity and select a match, or enter its exact ID
     and press Enter. Search requires an administrator token;
     exact-ID lookup also works with other tokens that can read the entity.
     Details must load successfully before you can create the button.
     State is selected by default; expand **Change value** for an attribute
     or a custom dotted path.
   * For **MQTT**, [configure a broker](../integrations/mqtt.md#configure-the-broker),
     enter one exact topic, and choose **Read topic**. Wildcards and
     broker-wide discovery are not supported. After a JSON message arrives,
     click the raw payload or a scalar field, or enter a **Custom path**.
     A scalar payload is selected automatically.
     Inspection waits up to ten seconds and releases
     its temporary interest after copying the sample. A received empty payload
     is valid text; a received zero can support numeric widgets. No message,
     timeout, or subscription failure cannot create a button. Retry or correct
     the topic. A truncated selection cannot create a guided draft; choose
     a shorter scalar value or use manual binding entry.
   * For **On-device bindings**, type at least three characters to search by
     scheme, key, or description, then select an entry. You can also enter a
     complete binding and press Enter. Its registered key or preset
     fills in the binding; only entries needing parameters open a parameter form.
     Static local values, including device-class values and listed format
     presets, supply realistic examples without reading the device.
     Numeric scenarios require explicit numeric capability metadata and a
     complete, finite, non-truncated sample; the declared example can supply
     that sample. An example alone never grants numeric capabilities. Entries
     without the required metadata stay label-only. Named pad binding reads use the saved pad
     definitions, not unsaved edits. Check scheme descriptions and examples for
     integration or context requirements when a value is unavailable.
4. Expand **Display options** only if you want to change formatting, unit text,
   or **When unavailable**. Numeric values default to **Number**, or
   **Whole number** for integer-only bindings where supported. Fixed numeric
   output and duration-specific syntax keep their native formatting.
   **Sample value (example)** is
   illustrative; the saved binding still uses runtime readings.
   Choose **Continue to button**. These options carry
   into the button and are not requested again during customization.
5. Under **What should this button do?**, choose **Show a value**,
   **Show a bar**, **Show a gauge**, or **Show a value with history** for a numeric reading, or
   **Toggle an entity** for a light or switch, or **Show a toggle switch** to add an interactive switch widget. Adjust the title and available controls.
   Value buttons show the name, formatted value, and unit in three labels.
   Bar buttons show the title, a single bar, and the formatted value with its
   unit below. Set a minimum and maximum, orientation, formatting, and unit.
   Catalog values with explicit integer metadata omit decimal controls.
   Local catalog units come from declared metadata and stay outside the
   binding token. MQTT units start empty; the builder does not infer them.
   Percentage entities start with a range of 0 to 100. Other readings get a
   suggested rounded range with headroom: 21.4 starts at 0 to 50, while -12
   starts at -50 to 50. A zero reading starts at 0 to 100. Check and adjust
   these suggestions for your source; they become fixed limits, not an
   automatically changing scale. The maximum must exceed the minimum.
   Ranges crossing zero automatically fill from zero toward the reading.
   **Show a bar** requires a numeric, non-truncated sample and bar-chart
   widget support. Colors, targets, additional bars, and other styling belong
   in the normal pad editor.
   **Show a gauge** requires a numeric, non-truncated sample and gauge
   widget support. It shows the title, a single semicircular ring with a needle
   and five interior ticks, and the formatted value with its unit below.
   Choose minimum, maximum, available formatting, unit, and arc color.
   It uses the same range suggestions as the bar, but fills from the minimum.
   Additional rings, targets, geometry, and other styling remain available in
   the normal editor; see [Gauge](../widgets/gauge.md).
   **Show a value with history** requires sparkline widget support and
   non-truncated readings. With one value, the button shows its title,
   formatted current value, and trend. Choose **Add another value** to compare
   sources, including values from different providers. Two or three values
   automatically use the multi-value layout with a chart title and a colored
   line and current-value label for each value. Each value has its own display
   options and line color; **Edit value** and
   **Remove value** affect only that value. Canceling selection preserves the
   chart design. An unavailable Home Assistant reading with a reported unit
   can also be selected; its preview label shows `---` rather than plotting
   zero. This exception does not apply to MQTT or catalog values.
   Choose a shared time range (1 hour, 5 hours, 6 hours, 24 hours, 7 days, or
   30 days)
   and requested sample interval in seconds. The preview caption shows the actual
   interval after the 1024-point capacity limit is applied.
   **Automatic (based on source units)** uses a shared scale only when every
   value has the same known, nonempty provider-reported unit. Unknown units,
   including MQTT units, use independent scales even when you enter matching
   display text. You can explicitly choose either scale mode. Independent
   scales show trends, but line heights are not directly comparable.
   Changing display units does not convert or normalize readings.
   The chart starts with 24 hours, a requested five-minute interval, a green
   first line, and latest-value dots. Additional lines start blue and purple.
   Devices with Home Assistant history support load recorded history using
   the mean statistic for eligible HA series only. MQTT and built-in series
   collect live values while the device runs. Statistics, reference lines,
   and other styling remain available in the
   normal editor. Resize the generated button there if the chart needs more space;
   the builder does not expand into neighboring positions.
   Only supported Home Assistant lights and switches offer a toggle; MQTT and
   built-in sources never infer commands from their values.
   Toggle buttons have a Material Symbol, on/off colors, and a toggle tap action.
   Lights default to a lightbulb icon; switches default to a switch icon.
   Select an on/off color badge to open the shared color picker. Choose a palette
   color or enter a custom hex color, then choose **Apply**.
   **Show a toggle switch** adds a switch widget bound to the entity state. Its
   on and off positions send separate turn-on and turn-off actions. This is
   distinct from **Toggle an entity**, which uses a single toggle action on a
   regular button.
6. Check the approximate **Preview**, then choose **Create button**.
   Preview uses fetched data rather than continuous updates. The bar preview
   reflects the chosen range and orientation. The gauge preview reflects its
   range and arc color; out-of-range readings stop the indicator at the limit
   while the label still shows the actual value. Toggle preview states are
   simulated and never send commands. The history template shows fixed,
   illustrative trends, not actual history, with a left-aligned title and colored
   current-value labels beside the right-hand line endpoints. Those labels use
   fetched readings and their configured units. Changing the time range updates
   the caption, not the sample curves.
7. Customize the generated button in the normal inspector if needed, then
   **Save Pad**.

Changing a unit changes display text, not the measurement. Missing HA values
show `---`; an unavailable bar reading does not become zero, and the device
retains its last bar position. A toggle button's icon uses pink-red for
unavailable data, distinct from its on/off colors.
Its color follows reported HA state, not an assumed result
of tapping. Material Symbol assets use the normal icon-generation path on save
and need its browser font/network resources.

The builder is a one-time creation tool, not a special button type. It cannot
reopen an existing button's builder choices. **Cancel** leaves the position
unchanged, and **Create button** changes only the draft.

## Keep track of edits

The editor maintains one in-memory draft. Switching buttons, inspector tabs,
or Pad/Button scope retains incomplete edits. There is no button Apply/Cancel.
A failed save keeps the draft for correction or retry.

Leaving or switching pads offers **Keep Editing** or **Discard and Switch**.
Browser reload/close warns about unsaved changes, but drafts are not durably
stored or retained for several pads.
On mobile, **Return to pad** retains edits; the inspector also has **Save Pad**.

Device-wide defaults save separately. Clearing, importing, pasting, filling,
and block placement change the draft unless the operation explicitly says it
writes immediately; recipe installation and device import are separate paths.

## Move and resize

On desktop, drag a configured button to an empty position. Valid destinations
have a green outline. Drop on an occupied position or press Escape to cancel.
A multi-position span is preserved where it fits, otherwise movement can reduce
it to 1 x 1. Check the result before saving.

Select a button to show an external selection frame and four persistent resize
grips at its edge midpoints. The borderless grips are white on most button
backgrounds and dark on very light backgrounds. Drag a grip to resize along
that edge, or edit Column span/Row span explicitly. Hovering an unselected
button also reveals its grips.

Click the empty canvas around the preview to deselect the button and return
to Pad settings without discarding edits.

Covered positions belong to the spanning button; do not hide useful controls
under it. Copy/paste is an alternative and works on mobile where dragging is
unavailable.

Numeric inputs accept typing and keyboard arrow keys. Scrolling the inspector
does not adjust the hovered numeric value.

## Configure the pad itself

Pad **Appearance** owns its background override. Pad **Bindings** owns reusable
[named data](bindings.md#name-a-source-on-the-pad). Pad **Actions** owns
[full-screen tap and swipe behavior](actions.md).

Wake Screen controls a direct display-sleep return destination. A temporary
idle pad returns to the screen that preceded it instead; its own Wake Screen
is not used for that transient session. See
[display and sleep](../device/display-and-sleep.md).

Before experimenting with an existing layout, [export it](../device/backup-and-restore.md).

[Pad tasks](README.md) | [User guides](../README.md)
