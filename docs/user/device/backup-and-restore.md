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

## Export saved device configuration

Save each changed portal section and pad first. In **Pads > Pad Editor >
More**, choose **Export Device Config**.
Store the file alongside your separate asset/feature backups and note the
firmware version and target.

Pad reads that fail during export can become empty entries in the file, and
failed default-setting reads can leave those settings empty.
Inspect important saved pads/defaults and keep individual pad exports as well;
do not assume a downloaded file is proof that every read succeeded.

## Import device configuration

> [!WARNING]
> **Import Device Config** writes settings and pad files immediately and reboots.
> It can overwrite current pads or delete slots represented as empty in the
> export. It is not a reversible preview or an all-or-nothing transaction.

1. Export the destination's current configuration and preserve any unsaved drafts.
2. Check that the destination firmware supports the source's widgets/actions
   and layout limits.
3. Use **Import Device Config**, choose the file, and confirm the overwrite.
4. Wait for completion and reboot. Do not interrupt power.
5. Reopen the portal. Check each important pad and feature, re-enter missing
   credentials, and restore required assets/packages.

Network identity settings are excluded from normal device export, so an
ordinary import is not a Wi-Fi-provisioning tool.
On a different device, enabled integrations can need credentials even when
their non-secret settings were imported.

If import fails partway through, earlier writes can remain. The error can name
the failed pad. Inspect current state before retrying; restore from the
destination backup where necessary. Unsaved pad drafts are not protected by
device import.

## Preserve data before destructive operations

Use [Storage](storage.md) and feature-specific download/export controls where
available. For removable primary storage, power down before removing and copying
the card. A raw file copy is useful archival data but is not a promise of
cross-version restore compatibility.

Review [firmware and reset consequences](updates-and-recovery.md) before changing
partition layouts. Configuration export does not turn factory reset into an
undoable operation.

[Device tasks](README.md) | [User guides](../README.md)
