---
title: Install a recipe
description: Configure recipe parameters, place a scenario without overwriting buttons, and handle partial installation.
---

A recipe adds a prepared set of buttons to a selected pad and can also configure
related features. It is useful for installing a working starting point without
building every control by hand, but it is not a full-device backup or restore.
Recipe buttons are added to Main. Existing Overlay buttons remain in place, and
the device checks the shared button limit across both layers.

## Prepare the catalog

The device starts with an empty catalog. Open **Pads > Recipe Catalog** and
save a complete catalog, using the [repository samples](../../samples/recipe-catalog.json)
or your own [authored catalog](recipe-authoring.md).
Then open **Pads > Recipes**.

Recipes can install buttons, named bindings, fonts, and feature settings.
Inspect those changes and back up the target before installing an unfamiliar
catalog. Font URLs require browser access and trusted source files.

## Place the scenario

1. Select a recipe and check its requirements.
2. Fill in its parameters and choose the target saved pad.
3. Select an available position in the placement grid. The clicked position is
   always the top-left anchor of the full footprint.
4. Confirm any provisioning prompt and wait for all installation steps.
5. Use **Show Pad** to inspect the result, or **Navigate to Pad Editor** to edit it.
6. Follow any completion links to finish separate feature configuration.

Normal placement never overwrites existing buttons.
Adaptive recipes can choose a smaller forward-fitting footprint near an edge.
On an empty grid that is too small, the portal can offer the smallest supported
grid increase that fits.

When Main buttons block every placement, **Clear Main Buttons** confirms and
saves an empty Main button list. It retains Overlay buttons and other pad
settings and does not itself install the recipe. This is an immediate write,
not just an editor draft.

## Review feature changes

For example, the Alarm Clock sample requires alarm/audio support and a free
5 x 5 area. It asks before replacing the alarm's ring/stop actions and disables
the alarm while preserving its schedule/durations/lateness.
After installation, open Alarm Schedule, choose time/days, enable, and save.
Fonts requested by the sample can need downloading; unavailable fonts affect
appearance and carry the [normal font risks](../media/fonts.md).

## Recover from failure

Installation is not transactional. The pad saves before component changes;
a later failure can leave that pad and earlier component writes installed.
Read the error, inspect the target pad and affected settings, then retry only
the intended work. Do not repeatedly install into an already occupied area.
Use your [backup](../device/backup-and-restore.md) if you need to restore it.

[Pad tasks](README.md) | [User guides](../README.md)
