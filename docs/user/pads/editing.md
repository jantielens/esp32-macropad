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

Hover a button for resize handles or edit Column span/Row span explicitly.
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
