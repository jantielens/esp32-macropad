---
title: Browse device storage
description: Check storage backend, mount status, and free space, then use feature-specific tools to manage files.
---

Device storage keeps saved pads, other configuration files, and uploaded media
available between restarts. Depending on the firmware, these files live in
internal flash or on an SD card. The Storage page lets you browse files and
check available space.

## Check capacity and mount status

Open **Device > Storage** when available. Check the active backend, whether
it mounted, total capacity, and used/free space. A card-backed primary
filesystem also reports its card type.
The header's [health overlay](diagnostics.md#check-device-health) provides a
storage summary too.

Optional image caches are not necessarily primary storage.
Use your [hardware reference](../hardware.md) for the exact requirement.
Changing backend does not migrate existing files.

## Browse and download

Expand a folder to list its direct contents. Folders appear first, then files,
with alphabetical ordering within each group. **Open** views supported media
in your browser or downloads other file types. **Refresh** reloads storage
status and the root listing.

This browser is read-only: it does not upload, delete, or format files.
Downloads can contain private configuration or captured images; keep backups
and shared diagnostic attachments appropriately protected.

## Manage files through their feature

* Use [Image Library](../media/images.md) for button background images.
* Use [Alert Sounds and Music Library](../media/audio.md) for MP3 files.
* Use [Fonts](../media/fonts.md) for uploaded typefaces.
* Use [Camera > Snapshots](../media/camera.md#save-and-manage-snapshots)
  for the latest capture and dated camera roll.
* Use [Extensions](../media/extensions.md) for packages and slot state.

Deleting an asset can break pads or actions that still reference it.
The Storage browser is not a complete export/restore tool; plan
[backups](backup-and-restore.md) before deleting or changing firmware.

If storage fails to mount, fix the required card/backend before continuing.
Do not use factory reset as the first diagnostic step. Firmware without a
filesystem does not expose file-management features.

[Device tasks](README.md) | [User guides](../README.md)
