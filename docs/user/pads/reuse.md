---
title: Reuse layouts and controls
description: Choose shared defaults, template pads, building blocks, copying, and bulk operations without overwriting work.
---

Shared defaults, pad templates, and copying reduce repeated setup across pads.
Inheritance keeps selected settings connected to a shared source, while copied
content becomes a separate starting point. Choose between them based on
whether later changes should propagate.

## Choose the right kind of reuse

* Use [device defaults](appearance.md#choose-the-level-to-change) for a common look.
* Use a template pad for shared navigation/buttons and named bindings.
* Use a building block for an independent group placed once.
* Use a [recipe](recipes.md) for a parameterized scenario with provisioning.
* Use export/import for a [portable backup](../device/backup-and-restore.md).

## Share a template pad

Create and save a pad with common controls, then select it in another pad's
**Pad > Layout > Template Pad** selector. Templates inherit Main buttons only.
Source Overlay buttons are not inherited, and destination Overlay buttons do
not suppress Main inheritance. Template buttons fill empty Main positions;
local Main buttons win. Ghost overlays show inherited positions in the editor.
Named bindings merge too, with local definitions winning conflicts.

Templates do not chain: a template's own template is ignored.
Inherited buttons are merged at load, not copied into the destination file.
The combined count includes local Overlay buttons and inherited Main buttons;
the device rejects a template merge that would exceed its button limit.
Full-screen tap actions remain local and are not inherited.
Save the destination and inspect its effective layout.

## Place a building block

Choose a block under **More > Blocks**, then click a valid empty anchor in the
selected layer. Occupancy is checked within that layer, while the combined
button limit includes both layers.
Green ghost outlines fit; red indicates overlap/boundary failure.
Escape or Cancel leaves placement mode.
Availability depends on footprint, free positions, and button capacity.

After placement, buttons are ordinary independent draft content.
Changes to the original block do not update installed buttons.
Save Pad to apply.

## Copy, fill, and clear

Select a configured button and choose **Copy**, then select a destination in
the same or another pad and choose **Paste**. A dialog offers prefilled sources,
plain-text labels, and explicit color badges. Leave values unchanged to keep
them, or edit the values you want to replace. Choose **Paste** or **Paste with
changes**, then review the button in the normal inspector and **Save Pad**.
**Cancel** leaves the destination unchanged.

Home Assistant entity replacements update matching references in labels,
widget inputs, expressions, and sparkline history settings while retaining each
binding's formatting and fallback. MQTT and other bindings use complete-token
textboxes: replace the brackets and contents together. Identical tokens share
one replacement; different paths or formatting remain separate. For example,
replace `[health:cpu]` with `[health:wifi_rssi]` to display Wi-Fi signal strength.
Review fixed widget ranges and units when changing the type of value.

Matching explicit colors share a picker, including literal label-style colors.
Colors for unconfigured sparkline series and reference lines are omitted;
their settings are still copied unchanged.
Inherited colors and color constants inside expressions are not offered.
Identical plain-text labels share one replacement field.
Mixed labels expose their bindings rather than a second whole-label editor.
Hover a row's label to see which configuration fields it changes.

This is an independent copy, not a linked template. Actions and their control
targets, extension-specific configuration, and other settings remain unchanged.
Named pad bindings use the destination pad's definitions; copying a button does
not copy those definitions. Appearance can change when the destination has
different inherited defaults.

Fill Pad repeats the copied configuration; check which existing content it
will replace before saving. Copy/Paste Pad transfers the layout and settings
including both button layers into the current draft. Export/import also retain
both layers. Review template references and bindings afterward.

Clear Pad, paste, fill, and pad-file import change the draft.
Export/save work first, then Save Pad only after checking the intended result.
These operations are not device-wide undo or multi-pad draft storage.

Clear Pad starts an empty 3-column, 2-row pad with Auto row heights and column
widths. It removes both button layers, the name, wake screen, bindings,
full-screen tap actions, template selection, and pad appearance overrides.
Device-wide defaults and copied buttons or pads remain available.

Recipe **Clear Buttons** is different: it confirms and writes an empty button
list immediately while retaining other pad settings. See the recipe workflow.

[Pad tasks](README.md) | [User guides](../README.md)
