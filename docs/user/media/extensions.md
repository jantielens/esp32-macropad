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
Labels and icons share the button with the extension's content region.

Leave **Tick interval (ms)** empty to use the package default, or enter a whole
number from 33 to 1000. Zero is not valid.
Short intervals can increase display/CPU work; use slow-changing content on
slow-refresh displays.

For canvas-based content, **Upscale** 2x, 3x, or 4x reduces the allocated canvas
dimensions and scales the result to the button, trading detail for memory.
The default 1x keeps the native appearance. Non-canvas controls are not scaled.
When the available region changes size, the Extension instance restarts and
its in-memory state resets.

Button action lists can run when the Extension passes through tap/hold input;
an Extension that consumes the event can handle it itself.
Check the package's interaction behavior before relying on an action.

## Use Calculator

Install the Calculator package and place its Extension widget on a button
large enough for the keys. Leave **Upscale** at 1x: Calculator uses interactive
controls rather than a canvas. If the region is too small, it shows
`Make widget bigger`; enlarge the button or its widget region.

The default configuration is empty or `{"display_style":"normal"}`.
For an interactive e-paper display, use
`{"display_style":"epaper"}` to avoid pressed-key flashes. The display's
firmware may suppress flashes even in normal mode. Refresh speed and ghosting
depend on the display, and rapid touches may not appear immediately.

You can set `font`, `corner_radius`, `keys_background`, and `keys_foreground`
in the configuration. Use `keys_overrides` to set `background` and
`foreground` independently for the `numbers`, `operators`, `functions`, and
`memory` key categories. Colors use `#RRGGBB`; unspecified colors inherit the
key defaults, then the owning button's colors. You can also style the result
display independently. For complete configuration examples, see the
[Calculator reference](../../../extensions/calculator/README.md).

Calculator executes operations as entered: `2 + 3 * 4 =` gives `20`.
For example, `200 + 10 % =` gives `220`, and `200 * 10 % =` gives `20`.
It supports square root, sign, clear, and `MC`, `MR`, `M+`, and `M-` memory
keys. Entries have up to seven digits and results use seven significant digits.
Division by zero, negative square roots, and results outside the supported
range show `Error`; use `C` to continue. Memory and calculations are temporary
and reset when the instance restarts, including on resize or reboot.

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
