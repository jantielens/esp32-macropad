---
title: Calibrate and use a Coffee Scale
description: Calibrate mass, choose a brew, follow pour stages, and manage saved brew records without hardware-specific setup.
---

Coffee Scale combines weight and flow readings with guided brew stages and
saved brew records. Use it to weigh a dose, follow a pour, and compare the
results of different brews; calibration establishes the mass reference those
readings depend on.

Check [board wiring/power notes](../../hardware.md), keep liquid away from
electronics, and place the scale on a stable surface before calibration.
Open the portal's Scale, Brews, and Brew Templates sections.

## Calibrate mass

1. Clear the platform and tare.
2. In Scale, choose Calibrate to open the calibration panel.
3. Place a known reference mass on the platform, enter its mass in grams, and
   select Calibrate in that panel. Wait for the success message.
4. Remove/reapply it and verify both zero and reference readings.
5. Choose Stable, Balanced, or Responsive filtering, then save.

Balanced is the default compromise between noise and response.
First-boot tare without saved calibration is not a substitute for a known-mass
calibration. Repeat if geometry/load-cell mounting changes or readings drift.

## Make a first brew

Select Free Pour, tare the assembled vessel, and start pouring.
The first-pour weight trigger starts the count-up timer.
Finish manually when complete. Follow the displayed stage/instruction rather
than assuming every template uses the same start/tare sequence.

For V60, capture coffee dose before preparing/taring the brewer.
Advanced V60 provides staged bloom/pour targets; some stages advance by time,
not weight. A displayed target is guidance, not a valve controller.
Do not leave a pour unattended while waiting for automatic completion.

Use supported Brew actions from pads or physical controls.
Live `[brew:...]` values include selected brew, stage, time, target/captured
weights, and flow; consult the device Binding reference.
A List with provider `brew_definitions` can select a template through its
selected ID. Configure the action on that List, not an assumed boot selection.

## Review and preserve records

Completion/stop saves the brew record with its sampled weight/flow series,
captures, and markers. In Brews, open a record to inspect charts and Export or
Delete. Export All/Import handles collections; Clear All is destructive.
The store is bounded and can evict old records. Export valuable brews outside
the device, and do not assume a configuration backup includes log history.

## Customize a workflow

Use [brew template authoring](templates.md) for the user-uploaded JSON language.
Inspect/download a built-in template before adapting it.
Uploading/reloading/deleting templates can stop an active brew to replace the
template registry. Finish and verify its saved record first.

[Specialized workflows](../README.md) | [User guides](../../README.md)
