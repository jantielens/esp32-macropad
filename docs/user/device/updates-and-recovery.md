---
title: Firmware updates and recovery
description: Choose OTA or full USB installation, verify the result, restore access, and understand factory reset.
---

Firmware updates replace the software that runs the device. Normal updates
and recovery flashing are different paths, and some installation choices can
also erase stored data or change its layout. Preserve your setup before
choosing an update or recovery operation.

## Before updating

1. [Back up configuration and separate data](backup-and-restore.md).
2. Identify the exact installed target and read its
   [hardware/update requirements](../hardware.md).
3. Check whether the destination uses the same partition layout. A variant or
   layout change can require a full USB installation and data restore.
4. Use reliable power and a stable connection. Finish uploads and save drafts.
5. Stop playback/streams and leave the device available during the update.

Not every target supports OTA. The presence of firmware controls alone does
not establish that an OTA application slot exists.

## Update online

For an OTA-capable device, open **Firmware > Online Update** and select
**Open Update Page**. The installer opens with the current device address.
Follow its release/target selection and Wi-Fi update steps.

The browser needs access to both the release site and the device's LAN address.
Use the configured portal credentials when requested, without putting them in
a shared URL. Keep the page open and power connected until the device restarts.

## Upload a firmware application file

Open **Firmware > Manual Upload**, select the correct application `.bin` for
this target, then **Upload Firmware**. This writes the OTA application slot.
Do not upload a merged full-flash image, bootloader, or partition-table binary
through the application uploader.

The progress/reconnection dialog is not proof of a successful new boot.
After reconnection, check **Firmware > Version Info**, the header's firmware
version, and one representative device function. A network error near upload
completion can coincide with reboot; verify the installed version before
retrying or concluding success.

Firmware updates pause some background activity and stop audio; Music does
not automatically resume afterward.

## Use a full USB installation

Use the [firmware installer](https://jantielens.github.io/esp32-macropad/) and
your board's connector/download procedure for first installation, a target
without OTA, recovery, or a required partition migration.

> [!WARNING]
> An OTA application image does not update the partition table. Changing layout
> can move or shrink stored data. Back up first, follow the board's erase/full
> flash requirements, then restore. Do not assume existing files are preserved.

Do not guess a button sequence from a different board. Power/reset buttons,
runtime configuration entry, and chip download mode serve different purposes.

## Recover without erasing

If the firmware runs but the portal is unavailable:

* Try its IP address instead of `.local` and check
  [Wi-Fi/network settings](network-and-access.md#restore-access).
* Wake a sleeping device or use its board-specific configuration-entry control.
* In setup mode, join the device AP and open <http://192.168.4.1>.
* Check [logs and reset information](diagnostics.md) if boot is unstable.

Configuration mode does not erase settings. Use it before factory reset.
If boot fails, use the board's documented serial download recovery.

## Open a sleeping sensor node's portal

For a headless sensor node using MQTT or BLE duty cycling, press reset or
disconnect and reconnect power once. The device temporarily enters Config mode
and connects to saved Wi-Fi; open its usual portal address. If Wi-Fi cannot
connect, join its setup AP instead. The saved operating mode is not changed.
Waking from deep sleep continues normal duty cycling without opening this window.
E-Paper Frames retain their board-specific recovery controls.

In **Device > Operating Mode**, **Recovery Portal Auto-Sleep** controls how long
the portal may be idle. Portal requests extend the window, and firmware or
configuration uploads prevent auto-sleep while in progress. At timeout the
device sleeps for the saved Wake Interval, then resumes duty cycling.
Zero disables auto-sleep. Board defaults may differ; existing saved values
remain unchanged after a firmware update.

Software restarts, including after updates, and crash resets also open this
temporary window. Repeated resets can therefore increase battery use.

## Put the device to sleep after configuration

Use **Device > Operating Mode > Sleep now** to end a maintenance session
without waiting for recovery auto-sleep. This control is also available on
devices saved in Always-On mode.

1. Save any changes you want to keep. **Sleep now** does not save them.
2. Set **Sleep duration (seconds)**, from 1 to 65535. It starts with the saved
   Wake Interval (at least one second); use a shorter duration to test the next
   duty-cycle wake.
3. Select **Sleep now**. Wait for the accepted message, then close the tab.

The device disconnects and enters deep sleep. Running timers and playback stop.
The duration applies once and does not change the saved operating mode or Wake
Interval. On timer wake, duty-cycle devices resume their cycle; an Always-On
device returns to normal operation.

Sleep is refused during firmware updates, configuration uploads/writes, or
another network transition. Finish that operation before retrying. The portal
does not poll for reconnection after sleep. Other open tabs can still extend a
later recovery window through their automatic requests.

## Recover setup access with a power cycle

If you need to force the setup AP, power the device on, interrupt power
during the first ten seconds and before it intentionally sleeps, then power it
on again. The second qualifying power-on starts the setup AP without changing
the saved operating mode or AP password. A short startup or duty-cycle sleep
can end the recovery window earlier; the device does not delay sleep to keep
the full ten seconds available.

The device cannot measure how long power was disconnected. If power is removed
while the recovery window is armed and restored later, the next power-on can
still start the setup AP. This procedure does not bypass the AP password.
Follow [network and access](network-and-access.md#restore-access) to reconnect.

## Factory reset

Open **Device > Factory Reset** and confirm only after backing up.
The device erases NVS settings and restarts into setup mode. Wi-Fi, portal
credentials, integrations, and BLE pairings must be configured again.

With internal primary file storage, reset formats that filesystem, removing
pads, defaults, action lists, alarm/timer definitions, media, fonts, snapshots,
and stored records. With SD-primary storage, it removes firmware-owned
directories rather than formatting the entire card; unrelated files outside
those directories are left alone. Without a filesystem, reset clears NVS only.

> [!CAUTION]
> Factory reset is destructive and cannot be undone by the device.
> It is not a complete flash sanitization procedure: do not assume separate
> native Extension slots, retained crash data, or unrelated card files were
> erased. Remove private backups and use appropriate full-erasure procedures
> before transferring a device or card to someone else.

After reset, follow [getting started](../getting-started.md), then restore
only the data you intend to keep.

[Device tasks](README.md) | [User guides](../README.md)
