---
title: Fonts and device typography
description: Upload trusted static TrueType fonts, select inherited typography, and handle replacement and memory limits.
---

Fonts determine the appearance and size of text on the device. Uploaded fonts
add choices beyond the built-in typefaces and can be selected for device
defaults or individual labels.

Font upload is available on eligible interactive display firmware with storage
and working PSRAM. Other firmware retains builtin Montserrat bitmap fonts.
Font choices do not change portal text, icons, grid geometry, or sleep-first
Frame text.

## Upload a font

1. Open **Pads > Fonts**.
2. Choose a trusted static TrueType-outline `.ttf` file.
3. Give it an ID of 1-31 lowercase letters, digits, `_`, or `-`, without `.ttf`.
4. Select **Upload** and wait for the success result, not only transfer progress.

Variable fonts, font collections, CFF/OpenType, WOFF, and WOFF2 are unsupported.
The catalog lists stored IDs and sizes. **Refresh** reloads the list.
There is no conversion service or font preview.

> [!WARNING]
> Only upload trusted fonts. Format checks are not a hardened font sandbox.
> Font allocation failure can hang or crash the device, even with a valid file;
> recovery is not guaranteed to be an automatic watchdog reboot. Large sizes
> and many cached pad/font combinations increase memory pressure.

## Set the device default

Open **Pads > Pad and Button Defaults > Device Typography**.
Choose Montserrat or an uploaded font and leave Base Text Size blank for
Automatic, or enter 1-65535 px.
Save the defaults and reboot to apply device-default typography changes.

Family and size inherit independently. Automatic text roles scale from the
device's medium text size; an explicit size remains absolute.
This does not automatically fit text into its button or resize icons.
Start with a moderate size and check the physical display.

## Override a label

In the button's label settings, open the **Aa** raw style input.
For example:

```text
font:my-font;font_size:96
```

Omit `font_size` for automatic label sizing.
Use `font:@builtin` to select Montserrat while retaining automatic sizing.
Save the pad. There is no separate per-label font picker.
The [label style reference](../pads/appearance.md#use-label-style-syntax)
covers other style declarations and inheritance.

For TTF text clipped because of vertical font metrics, try:

```text
font:my-font;font_metrics:bounds
```

`bounds` uses whole-font bounds for line height/baseline.
A numeric multiplier such as `font_metrics:2` is another option.
Neither changes glyph size or fixes a label wider than its button.
Different sizes/metrics create additional font instances; reuse combinations.

Bitmap text and failed TTF requests use the nearest builtin size among
12, 14, 18, 24, 32, and 36, with ties upward and endpoint clamping.
An unavailable selection remains saved; a fallback is not proof the uploaded
font loaded.

## Replace or delete

Delete an existing ID before uploading its replacement. Referenced fonts can
be deleted: affected cached pads rebuild with automatic bitmap defaults.
Uploading that ID again lets missing references resolve on a later rebuild.
Explicit pad instances can reload after pad-cache rebuild; device-default
instances are retried/applied at reboot.

Font files persist on the selected storage backend but are removed by
filesystem reset. Keep original files and license information in your
[separate asset backup](../device/backup-and-restore.md).

[Media tasks](README.md) | [User guides](../README.md)
