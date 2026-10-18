---
title: Network and portal access
description: Change Wi-Fi, hostname, static IP, and portal authentication without losing track of the device.
---

Network settings connect the device to your Wi-Fi and determine how you find
it on the local network. Portal authentication controls who can configure it
through a browser; it is separate from credentials for external services.

For first connection, use [getting started](../getting-started.md).
These steps change an already configured device.

## Change Wi-Fi or device name

1. Open **Device > WiFi** and enter the network name and, when changing it,
   the new Wi-Fi password. Use a 2.4 GHz network.
2. Choose what the device should do if saved Wi-Fi fails during normal boot:
   start the setup AP or continue offline. This preference does not change
   runtime Wi-Fi recovery or duty-cycle behavior.
3. Optionally set a **Setup AP password**. Use 8-63 printable ASCII characters;
   leave it empty for an open AP. The device displays the active AP name,
   password, and URL during setup. A saved password change applies the next
   time the setup AP starts, not to an AP that is already active.
4. Select **Save**. The connection does not move until reboot.
5. To rename the device, open **Device > Device Name**, enter the name, and
   note the generated mDNS hostname. Save that section separately.
6. Reboot when ready. Reconnect through the new network/address.

The WiFi page groups your network credentials under **WiFi Network** and the
recovery/offline controls under **Setup AP & Fallback**, with one **Save**
button below both sections.

Prefer a distinct name for each device. Use the displayed hostname rather than
assuming spaces or punctuation in the friendly name appear in the URL.
A router's DHCP client list can identify the device when `.local` discovery
is unavailable.

## Continue offline

If normal boot cannot connect to saved Wi-Fi and the preference is **Continue
offline**, the device turns off its Wi-Fi radio and starts its local controls.
Local buttons and BLE features remain available; network services do not.
If the device instead starts the setup AP, select **Stop AP and continue offline** on a
touch display to stop the AP and continue in the same boot. This live choice is
temporary and does not change the saved failure preference.

The button changes to **Starting offline...** while local features initialize.
If an update or configuration save prevents the transition, wait for it to
finish and try again; the setup network stays active. A busy request also
shows a retry message instead of silently ignoring the tap.

The setup display gives numbered steps for joining its network, opening the
portal, and saving your home Wi-Fi details. On LVGL displays, it follows your
boot font and colors and shows the selected boot logo when space permits.
The logo stays compact so the instructions and two-line offline button have
priority on smaller displays.

## Use a static IP

Open **Device > Network**. Leave **Fixed IP Address** empty for DHCP.
If assigning an address manually, provide the subnet mask and gateway and
ensure the address does not conflict with another client. DNS Server 1 and 2
are optional; DNS 1 defaults to the gateway when omitted.

Save and reboot to apply. Record the address before rebooting. A DHCP
reservation in the router can be easier to maintain than per-device static IPs.

## Require portal login

In **Device > Network > Security**, enable **Basic Authentication** and set a
username and password. A password is required the first time it is enabled.
Save and reboot, then confirm that opening the portal requests the credentials.
Keep them in a password manager.

> [!WARNING]
> The device portal uses HTTP. Basic Authentication is not transport encryption.
> Keep the portal on a trusted LAN; do not port-forward it to the internet.
> Use a trusted private network connection for remote administration.

The setup AP remains open for provisioning and recovery. After initial setup,
it cannot change an existing portal's Basic Authentication settings. Make those
changes through the authenticated portal on your normal network.

## Understand saved credentials

Home Wi-Fi passwords, tokens, and API keys are write-only. **Saved on device**
means a value exists; the portal does not display it. An empty field keeps the
stored value, while entering a new value replaces it on Save. Empty does not
mean "clear this credential".

The setup AP password is the exception: it is ordinary shareable
configuration, returned by the normal config API and displayed as plain text
in the portal and setup instructions. Anyone who can see the device screen,
including a retained E-Paper setup image, can read the active password. A
change applies at the next AP start.

On a fresh device only, an empty Wi-Fi password selects an open network.
Do not expect leaving an existing password field blank to switch to an open
network or remove a secret. Use the feature's disable control when available;
see [reset consequences](updates-and-recovery.md#factory-reset) before using
reset as a credential-clearing operation.

Do not include credentials in screenshots, shared URLs, pad examples, or issue
reports. [Configuration exports](backup-and-restore.md) do not recover saved
secrets for you. Keep an independent record.

## Restore access

* If `.local` fails, use the device's IP address. Multicast discovery may not
  cross guest networks, VLANs, or VPNs.
* If Wi-Fi fails, check the exact SSID/password, 2.4 GHz availability, signal,
  and router client isolation. Check static IP/subnet/gateway settings.
* If the setup portal does not open automatically, join the active AP shown on
  the device and enter its displayed URL, usually <http://192.168.4.1>.
  Temporarily stop mobile data/VPN routing if it redirects that request.
* If an expected AP is missing, the device may be connected normally or asleep.
  Follow its [hardware recovery procedure](../hardware.md).
* If you forget the AP password, use the existing [factory reset](updates-and-recovery.md#factory-reset)
  or the board's documented full-flash erase procedure. There is no password
  bypass gesture.

Configuration mode is not factory reset. Try it before erasing data.
For persistent boot failures, use [updates and recovery](updates-and-recovery.md)
and gather [diagnostics](diagnostics.md).

[Device tasks](README.md) | [User guides](../README.md)
