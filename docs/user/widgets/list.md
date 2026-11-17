---
title: Browse and activate List items
description: Configure a List source, row labels, item actions, and built-in or external navigation controls.
---

A List displays a bounded collection on a pad. Each row can show two labels
and run actions for its item. Lists sharing a source share its data, while
each List keeps its own focus and viewport.

## Configure a List

1. In the pad editor, choose **List** for a button's content.
2. Keep the generated **List ID**, or enter another ID unique on the pad.
   IDs contain 1-32 ASCII letters, digits, underscores, or hyphens.
3. Choose a **Source**. The available fields appear below it.
4. Configure the top and optional bottom label. For recent notes, try
   `[item:title|Untitled]` and `[item:duration_ms] ms`. Use **Aa** beside
   a label to override its style and **?** for the style reference.
5. Add item tap or long-press actions and choose **Save Pad**.

The source is one collection token, not a scalar binding or a filter. The
available sources depend on the device:

* `[pad:pads]`: saved pads in numeric order, with `id` and `title`
* `[voice_note:recent_notes]`: up to 20 recent notes, newest first; optional
  titles and creation times may be unavailable. Creation times are Unix
  timestamps in seconds, not formatted dates.
* `[shutter:tests]`: guided test definitions, with `id`, optional `title`,
  `shots_per_speed`, and `speed_count`
* `[health:metrics]`: local device health; see [Display device health](#display-device-health)

The default top label is `[item:title|Untitled]`. When a title is unavailable,
the label shows `Untitled`. In labels, unknown or missing item fields use the
specified fallback, or `---` when no fallback is provided. Check the source's
available fields if you see an unexpected fallback. Labels and row colors accept
normal bindings as well as item fields. Styles use the existing label style syntax;
List labels support `font`, `font_size`, `font_metrics`, `align`, and `color`.
Position offsets (`x`, `y`) and `font_upscale` are ignored. Labels always use
single-line ellipsis (`mode:dot`); other modes are rejected. Only `color`
accepts bindings. Under **Item layout**, **Visible items** defaults to **0 (Auto)**.
Auto fits as many rows as the List height allows using the label fonts, reserving
space for the bottom label when its template is configured. Rows target a
minimum height of 44 px, with at least 8 px of vertical breathing room per side
when space permits. Capacity stays independent of item count and resolved text.
Set **1-20** to choose an explicit row-slot count instead. Both modes distribute
remaining height across rows. Paged navigation reserves two slots when items
overflow, so five slots show three items plus the two controls. With navigation
enabled, explicit Paged counts must be at least three. Auto reserves at least
three slots when paging is needed, even if rows must be shorter than the target
minimum. A List too short for one comfortable row still clips its content;
if there is not even room for three one-pixel navigation slots, it shows
`List needs room for navigation` until resized.

Row background and border defaults come from device-wide button defaults.
You can override the background and border for all rows, and separately set
the selected row's background, border, and label colors under **Item appearance**
and **Selected item appearance**. Selected colors inherit the normal row colors except
the border, which uses the button border
color, or foreground color when the default button border has zero width.
The selected border defaults to twice the button default width, at least 2 px.

## Navigate and activate

Tap a row to select it and run its tap actions. A long press selects the row
and runs its long-press actions. Swipe within a scrolling List.
Choose **Paged** for a non-animated viewport on slow displays, or leave
**Viewport** at the board default. Item selection requires touch.

**Show navigation buttons** under **Item layout** is on by default and applies
only to Paged viewports. When items overflow, an up chevron occupies the first
row slot and a down chevron occupies the last. Tap them to move one page.
The controls stay in place while paging; up is disabled on the first page and
down on the last. Both disappear when all items fit without paging, returning
their slots to items. The controls inherit the containing button's colors and
border, and the List's corner radius. They use the default button tap sound,
respecting disabled audio, independently of any item sound actions.
Disabled controls stay silent. Holding a control still moves only one page
on release; it does not run item long-press actions.

Turn off **Show navigation buttons** to use all row slots for items and
external controls only. You can also use **Navigate List** actions targeting
the List ID for **Previous page** and **Next page** with built-in controls enabled.
Paging never selects an item or runs its actions, even when the selected item
is off-screen. A List starts with no selection; hiding it clears selection.

**Allow selection** under **Selected item appearance** is on by default.
Turn it off for a navigation List: rows still run their tap and long-press
actions, but do not retain a selected appearance.

Selecting a row uses the configured button feedback sounds even without item
actions. Rows with selection or actions use button press highlighting when
animations are enabled. Actions that provide their own audio suppress the
default feedback sound. Rows without selection or actions give no feedback.
Scrolling cancels the press feedback. Built-in navigation highlights only its
pressed control when animations are enabled; external paging does not flash
the List.

When items extend beyond the viewport, a position indicator appears to the
right. Its opaque track and indicator colors default to the device-wide button
background and foreground; override them under **Position indicator** if needed.
These colors accept scalar bindings, but not item bindings.

An item binding is available only while its row renders or its immediate
actions run. If an action references an unknown or missing field without a
fallback, the action fails and the remaining actions do not run. Provide a
fallback when appropriate. Before a delay, confirmation, or navigation, use
**Set variable** to capture `[item:id]`. Later actions can read that value as `[var:name]`.
For example, a pad chooser can navigate directly to `[item:id]` in the
item's Navigate to screen action. See [runtime variables](../pads/actions.md#set-a-runtime-variable).
For a Voice Notes detail pad, use **Load saved note** with `[item:id]` before
navigation instead. The action captures the ID without a variable; see
the [recording, list, and detail pad walkthrough](../media/voice-notes.md#build-recording-list-and-detail-pads).

If saving fails, binding syntax errors appear beside the field and in the editor
error message. Item-context errors checked by the device identify the List ID
and field; action errors also identify the tap or long-press action array and
the action's one-based position. For position indicator colors, replace item
bindings with scalar bindings. For actions after a context-ending action,
capture the item in a variable before that action.

Loading, empty, and source-error states appear in the List.
Lists require a display and PSRAM. Existing
provider/filter Lists and `[list:...selected]` bindings must be reconfigured;
they are not migrated automatically.

## Display device health

Choose **Device health** as the Source. Set the top label to `[item:title]`
and the bottom label to `[item:value|Unavailable] [item:unit]`. Turn off
**Allow selection** for a read-only display, leave item actions empty, and
choose how many items should fit on screen.

The source supplies raw numbers or text, not formatted labels or colors.
Memory values are bytes, uptime is seconds, and unavailable values are absent.
The optional `severity` field is `normal`, `warning`, `critical`, or `unknown`
for assessed metrics; informational rows have no severity.
An unknown assessment is not a healthy reading.

Use the existing label expressions to format numbers or map severity to
your preferred colors. For example, a bottom-label style can include:

```text
color:[expr:[item:severity|unknown]=="critical"?"#dc2626":"#808080"]
```

This example highlights critical values in red and leaves other values gray.
These are example colors, not source defaults. On monochrome displays, show
`[item:severity|]` as text instead of relying on color.

Health snapshots refresh about every two seconds while a health List is
visible; display updates still follow the device's binding and e-paper cadence.
Multiple health Lists share the same data. The old `[health:table]` and
`[health:extended_table]` bindings are no longer supported. Reconfigure those
buttons as Lists; saved pads are not rewritten automatically.

[Widget chooser](README.md) | [User guides](../README.md)
