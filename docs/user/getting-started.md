---
title: Install and configure your device
description: Choose firmware, connect the setup portal to Wi-Fi, and verify a first result.
---

A device runs firmware matched to its hardware and uses a local web portal for
configuration. Initial setup installs that firmware, connects the device to
Wi-Fi, and gives you a first saved control or specialized-device setup.

## Install the matching firmware

1. Find your exact target in the [hardware index](hardware.md). Read its power,
   USB, storage, and recovery requirements before connecting accessories.
2. Open the [firmware installer](https://jantielens.github.io/esp32-macropad/).
   Choose that target, connect a data-capable USB cable, and follow the installer's
   connection and flashing steps.
3. Use a full installation, including the partition table, for a first flash.
   If the device already contains data, [back it up](device/backup-and-restore.md)
   before flashing or accepting an erase operation.

An OTA application update is not a substitute for installing a different
partition layout. Exact download-mode buttons and USB connectors belong in
your board's hardware reference.

## Join the setup network

1. Power on and wait for the setup access point.
2. Join the open Wi-Fi network advertised by your device. A Macropad uses
   `ESP32-MACROPAD-XXXXXX`; other device classes use
   `ESP32-MP-<class>-XXXXXX`. The six-character suffix identifies the unit.
3. Stay connected even if your phone reports no internet access.
4. If the captive portal does not open, browse to <http://192.168.4.1>.

If the device has saved Wi-Fi credentials, it may join that network instead.
Use [network checks](device/network-and-access.md#restore-access) and your
board's configuration-entry procedure before considering a factory reset.

## Connect to your network

1. Enter the **WiFi network name (SSID)** and **WiFi password** for a 2.4 GHz
   network. On a fresh device, leave the password empty only for an open network.
2. Optionally set a **Friendly name**. Note the generated `.local` hostname.
3. Optionally enable **Require login for the portal** and set credentials.
   Store them securely.
4. Leave static IP unchecked unless you know the required network settings.
5. Select **Save & Connect**. This saves and reboots immediately.
6. Rejoin your normal Wi-Fi network on the phone/computer, then open the displayed
   hostname or the device's IP address from your router's client list.

The setup AP has no password. Configure it in a trusted environment.
Portal login applies on your normal network, not to the setup AP.
See [network and access](device/network-and-access.md) for credential behavior.

## Make a first Macropad button

For firmware with interactive pads:

1. Open **Pads > Pad Editor** and select a pad.
2. In the Pad inspector's **Layout** tab, choose a small grid, such as 2 x 2.
3. Select a button on the canvas. In **Content**, give its center label a name
   such as `Hello`.
4. In the Button inspector's **Actions** tab, add a tap **Show notification** action.
   Set its message to `Your pad is ready` and use a short display duration.
5. Select **Save Pad**, wait for success, then **Show on Device**.
6. Tap that button on the device and check that the notification appears.

Button edits are part of the pad draft; there is no separate Apply step.
**Show on Device** shows the saved pad and does not save a draft.
For a device without touch, use its configured physical navigation or
[Screen Preview](device/diagnostics.md#inspect-the-current-screen) when supported.

Continue with [pad authoring](pads/README.md), then
[export the saved configuration](device/backup-and-restore.md).

## Start a different device class

An [E-Paper Frame](specialized/epaper-frame/README.md) needs an image source and refresh
schedule, not a notification button. Instrument firmware starts with its
sensor/relay configuration and calibration. A headless sensor node has no pads.

Use the [specialized-device paths](README.md#use-a-specialized-device) and
your [hardware reference](hardware.md) to choose the appropriate first result.
The [portal guide](portal.md) explains why categories differ between devices.
