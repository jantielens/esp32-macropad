---
title: Install and use native Extensions
description: Choose compatible signed packages, stage slot changes, configure a button instance, and handle memory limits.
---

Extensions add native button content on firmware that supports executable
Extension slots. Check your [hardware reference](../hardware.md) for the target
ABI, usable capacities, and any full-flash migration requirement.

> [!WARNING]
> Install only trusted Extensions. Native code is not a sandboxed web widget
> and can affect device stability and memory use. A valid package signature
> checks package authenticity; it does not make all configurations safe.

## Install into a slot

1. Obtain a signed `.ext` package matching the target architecture and supported
   Extension ABI; do not select by title alone.
2. Open **Device > Extensions**. Check the selected slot's target, capacity,
   current package, and runtime status.
3. Choose **Upload** for an empty slot or **Replace** for an installed one.
4. Wait for success and the uploaded/reboot-pending status.
5. Save other work and reboot to install/load the staged package.
6. Return to Extensions and verify loaded status or the reported runtime error.

Unsigned or modified packages are rejected. Upload success is not proof that
the package loaded; an incompatible ABI requires a compatible rebuild and
re-upload. Slot capacity is usable ELF capacity, not the entire signed package
file size.

## Place an instance in a button

In **Pads > Pad Editor**, choose **Extension** content/widget for a button.
Select an enabled installed package, enter its package-specific Configuration,
then **Save Pad**.

Configuration is trimmed opaque text, not automatically validated as JSON.
Use the package's instructions and stay within the 511-byte limit.
Each placement has its own instance and configuration, even for the same package.
Labels, icons, and ordinary image controls are hidden for this content type.

Leave **Tick interval (ms)** empty to use the package default, or enter a whole
number from 33 to 1000. Zero is not valid.
Short intervals can increase display/CPU work; use slow-changing content on
slow-refresh displays.

For canvas-based content, **Upscale** 2x, 3x, or 4x reduces the allocated canvas
dimensions and scales the result to the button, trading detail for memory.
The default 1x keeps the native appearance. Non-canvas controls are not scaled.

Button action lists can run when the Extension passes through tap/hold input;
an Extension that consumes the event can handle it itself.
Check the package's interaction behavior before relying on an action.

## Disable, replace, or remove

Enable/Disable and Delete stage changes that require reboot.
A deleted package can remain loaded until then. The slot view distinguishes
staged replacement and pending deletion from the active runtime package.
Check state again after reboot.

Pad buttons can retain references to a missing/disabled package.
Choose a replacement or change their content before saving the intended layout.
The editor blocks saving an edited Extension when the catalog cannot be loaded;
do not interpret a load failure as an empty catalog.

Retain compatible signed packages separately from configuration exports.
See [backups](../device/backup-and-restore.md) before resetting or migrating the
filesystem/slot layout. To author packages rather than install them, use the
[Extension developer guide](../../dev/extensions.md).

[Media tasks](README.md) | [User guides](../README.md)
