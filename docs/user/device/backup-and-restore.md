---
title: Back up and restore configuration
description: Export a pad or device settings, understand excluded data, and restore without assuming a full-device backup.
---

Backups preserve settings and content so you can recover from a reset, change
firmware, or move a setup to another device. Configuration exports, pad exports,
and media downloads cover different parts of that setup; no single export
should be assumed to contain everything.

## Choose the backup scope

The export/import controls below live in **Pads > Pad Editor > More**.
Firmware without Pad Editor needs its own feature-specific export/download
controls and separate file backups; this menu is not a universal backup service.

> [!IMPORTANT]
> **Export Device Config is not a full-device backup.** It contains the main
> exported settings, saved pads, and device-wide pad/button defaults. It does
> not archive all stored files, every feature's configuration, or saved secrets.
> Device configuration exports cannot be imported back into the device.

Keep a separate backup of assets, packages, and instrument records you need.
Record critical feature settings before an erase, reset, or layout change.

| Data                                                                                 | Backup expectation                                                             |
| ------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------ |
| One pad                                                                              | Export Pad captures the current editor draft, including unsaved edits          |
| Main settings, saved pads, device-wide defaults                                      | Export Device Config reads saved device state, not unsaved form/pad drafts     |
| Wi-Fi, device name, static IP/DNS                                                    | Deliberately excluded from device export                                       |
| Saved main-setting passwords/tokens/API keys                                         | Not exported for recovery; retain your own secure record                       |
| Images, sounds, music, fonts, snapshots, instrument logs/sessions                    | Separate files, not embedded in the device export                              |
| Native Extension packages and runtime state                                          | Retain original compatible packages separately                                 |
| Alarm definitions, timer expiry lists, boot/swipe/physical-button/MQTT-trigger lists | Not collected by this export; record or separately back up their configuration |

Template pads and local assets can be dependencies of an exported pad. Include
them in your backup plan. A downloaded JSON file does not guarantee every
referenced file or service is available on another device.
Camera settings and motion actions also need separate backup; they are not
collected by Export Device Config.

Saved per-button image passwords are not returned by the device and cannot be
recovered from a device export. However, Export Pad includes the current draft,
so newly entered passwords can be present. Action text and other fields can
also contain secrets. Treat exports as private, not as sanitized public samples.

## Export and restore one pad

1. Open **Pads > Pad Editor** and select the pad.
2. Under **More**, choose **Export Pad** and store the downloaded file.
3. To restore, select the intended destination pad and choose **Import Pad**.
4. Inspect the imported draft, dependencies, and validation errors.
5. Select **Save Pad**. Import alone does not write it to the device.

Import replaces the editor's current pad draft. Save or export work you want
to keep first. Files are supported import/export artifacts; you do not need
to edit their persisted schema by hand.

If a saved pad cannot be loaded, the editor blocks saving to protect its
contents. Reload it after correcting the error. Opening or importing a pad
with an older action format removes its actions from the draft and shows a
notification; other settings are retained. Save only after reviewing the draft.
Newer action formats remain blocked. To replace an incompatible saved pad,
choose **Import Pad** with a supported file or **Clear Pad**, confirm the
replacement warning, and save the new draft. If the device cannot read the
saved pad, repair storage or restore the file from a backup first.

## Export saved device configuration

Save each changed portal section and pad first. In **Pads > Pad Editor >
More**, choose **Export Device Config**.
Store the file alongside your separate asset/feature backups and note the
firmware version and target.

Export reports a failure if it cannot read a pad or the device-wide defaults.
Keep individual pad exports for restoring pads. Device configuration exports
are for reference; they cannot be imported as device configurations or pads.

## Preserve data before destructive operations

Use [Storage](storage.md) and feature-specific download/export controls where
available. For removable primary storage, power down before removing and copying
the card. A raw file copy is useful archival data but is not a promise of
cross-version restore compatibility.

Review [firmware and reset consequences](updates-and-recovery.md) before changing
partition layouts. Configuration export does not turn factory reset into an
undoable operation.

[Device tasks](README.md) | [User guides](../README.md)
