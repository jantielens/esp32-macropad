---
title: Use the configuration portal
description: Open the device portal, find supported sections, and distinguish saving, live controls, and rebooting.
---

The configuration portal is a website served by the device itself. You use it
to change settings, manage content, and build pads without editing firmware.
Its available sections depend on the installed features, and each section has
its own save or live-control behavior.

## Open the portal

During setup, join the device's access point and browse to
<http://192.168.4.1>. After setup, use `http://<hostname>.local` or the IP address
assigned by your router. The phone/computer must be able to reach the device's
network. Sign in if portal authentication is enabled.

Start with [network and access](device/network-and-access.md) if the address
does not work. Sleeping devices may need to be woken using their
[hardware controls](hardware.md).

## Find a setting

Expand a sidebar category, then select its section. On a narrow screen, open
the navigation menu first. Opening a category closes the previous one;
collapsing it does not change the displayed section. Browser Back and Forward
return to visited sections. You can bookmark a section's URL.

Common paths include:

* **Device** for Wi-Fi, device name, network/login settings, timezone, storage,
  Extensions, logs, and factory reset
* **Display** for brightness, rotation, system screens, screen saver, preview,
  and image library
* **Pads** for pad editing, defaults, recipes, fonts, and dialog appearance
* **Actions** for boot/swipe/physical-button/MQTT triggers and timers
* **Alarm** for Schedule and Behavior
* **Connectivity** for MQTT, keyboard/mouse/gamepad, Home Assistant, and MCP
* **Audio**, **Camera**, and **Sensors** for their configured capabilities
* **Firmware** for online update, manual upload, and version information

Unavailable features are omitted. Device classes can add or prioritize their
own categories. A missing section is not a reason to copy another board's
settings; check your installed target's [hardware reference](hardware.md).

The header identifies the device and firmware. Select its CPU/health badge
for [diagnostics](device/diagnostics.md). The theme control changes portal
appearance, not the device's pad colors. Text navigation remains usable when
internet-hosted icons are unavailable.

### System screens and boot actions

Open **Display > System Screens** to set the shared font size, text color,
background color, and logo for the boot, Wi-Fi setup, and info screens.
The setup and info screens show the logo when space permits.
Select **Save Appearance**, then reboot to apply your changes. Uploading a logo
stages it; saving selects it. Deleting uploaded logos selects the device logo
for the next boot.

The info screen shows your device name, connected Wi-Fi network, and the IP and
`.local` addresses you can open to configure the device. When disconnected,
it shows connection guidance instead of stale addresses.

Use **Actions > Boot Actions** to configure actions executed on startup.
**Save Boot Actions** saves only those actions, independently of screen appearance.

## Save deliberately

Most sections have their own **Save** control. Save before leaving a section;
unsubmitted form values are not a device-wide draft. A save submits that
section's settings, not settings from every category.

* Live changes, such as moving the brightness slider, can affect the device
  before they are saved. Save to keep the setting across reboot.
* Settings that apply live, such as screen saver settings and timezone, do not
  need a reboot after a successful save.
* Wi-Fi, device name, static IP/login settings, keyboard transport, and some
  display/typography settings require reboot. Save first, then use the
  **Reboot required** banner or header reboot control.
* Upload, delete, and runtime controls act immediately unless their section
  explicitly says the change is pending reboot.

The header reboot control asks for confirmation and **does not save edits**.
The first-boot **Save & Connect** action is different: it saves and reboots as
one operation.

## Pad drafts are separate

The Pad Editor keeps one in-memory pad draft. Button and inspector-tab changes
remain in that draft until **Save Pad** succeeds. **Show on Device** displays
the saved pad, not unsaved edits. Leaving a dirty pad offers to keep editing
or discard; a reload or closed tab can lose the draft.

Device-wide pad/button defaults have their own save control.
An explicit [device-configuration import](device/backup-and-restore.md) writes
settings and pads and reboots; it is not an ordinary draft import.

After a reboot, wait for reconnection. If the name, IP, or Wi-Fi changed, open
the new address manually and verify the saved setting.
