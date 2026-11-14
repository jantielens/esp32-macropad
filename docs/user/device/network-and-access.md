---
title: Network and portal access
description: Change Wi-Fi, hostname, static IP, and portal authentication without losing track of the device.
---

Network settings connect the device to your Wi-Fi and determine how you find
it on the local network. Interactive devices can also boot Offline and enable
Wi-Fi or an AP only when needed. Portal authentication controls who can configure it
through a browser; it is separate from credentials for external services.

For first connection, use [getting started](../getting-started.md).
These steps change an already configured device.

## Change Wi-Fi or device name

1. Open **Device > WiFi** and enter the network name and, when changing it,
   the new Wi-Fi password. Use a 2.4 GHz network.
2. Choose what the device should do if saved Wi-Fi fails during normal boot:
   start the AP or continue offline. This Always-On preference does not change
   runtime Wi-Fi recovery or duty-cycle behavior.
3. Optionally set an **AP password**. Use 8-63 printable ASCII characters;
   leave it empty for an open AP. The device displays the active AP name,
   password, and URL during setup. A saved password change applies the next
   time the AP starts, not to an AP that is already active.
4. Select **Save**. The connection does not move until reboot.
5. To rename the device, open **Device > Device Name**, enter the name, and
   note the generated mDNS hostname. Save that section separately.
6. Reboot when ready. Reconnect through the new network/address.

The WiFi page groups your network credentials under **WiFi Network** and the
recovery/offline controls under **AP & Fallback**, with one **Save**
button below both sections.

Prefer a distinct name for each device. Use the displayed hostname rather than
assuming spaces or punctuation in the friendly name appear in the URL.
A router's DHCP client list can identify the device when `.local` discovery
is unavailable.

## Continue offline

If normal boot cannot connect to saved Wi-Fi and the preference is **Continue
offline**, the device turns off its Wi-Fi radio and starts its local controls.
Local buttons and BLE features remain available; network services do not.
If the device instead starts the recovery AP, select **Stop AP and continue offline** on a
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

## Boot Offline

On supported interactive firmware, open **Device > Operating Mode**, choose
**Offline**, select **Save Operating Mode**, and reboot. Unlike **Continue
offline**, this mode skips Wi-Fi initialization and connection attempts entirely.
The normal pad, touch, physical buttons, and supported Bluetooth actions remain
available. It is not deep sleep and does not promise battery runtime.

Clock labels and alarms need valid time. Firmware with an enabled RTC can
restore it during Offline boot after an initial network synchronization;
without RTC support, this boot needs network synchronization first.
See [time without Wi-Fi](time.md#time-without-wi-fi).

Before rebooting, configure a local pad or physical-button **Device command >
Enable AP** action so you can reopen the portal. Use the existing physical
recovery procedure if no such action is available. Configure initial actions
over station Wi-Fi or preload configuration; the first-time AP wizard remains
limited and does not offer full pad authoring without a Wi-Fi network.

Saved Wi-Fi credentials are optional in Offline mode. Keep them if you want
**Connect to Wi-Fi** to use that network later. Configure an **AP password** under
**Device > WiFi** if you want to protect direct AP access. See the
[BTHome remote workflow](../integrations/home-assistant.md#use-bthome-with-offline-boot)
for controls that need no device Wi-Fi connection.

## Enable network access from an action

Use **Device > Device command** in a pad or physical-button action list:

* **Connect to Wi-Fi** turns Wi-Fi on, tries saved credentials, and replaces an active AP. The device
  remains interactive while connecting; failure turns Wi-Fi off.
* **Disconnect from Wi-Fi** suppresses reconnection and turns station Wi-Fi off. It
  does not stop an AP.
* **Turn off Wi-Fi** stops network and AP access and suppresses reconnection,
  regardless of which is active. Supported Bluetooth controls remain available.
* **Enable AP** replaces station Wi-Fi with the device's AP and captive DNS,
  keeps the current pad active, and exposes the normal full portal.
* **Disable AP** stops AP access without reconnecting station Wi-Fi.
* **Restart into AP** reboots into restricted configuration/recovery, rather
  than keeping the interactive runtime.

An action-enabled AP stays active until disabled, replaced by station Wi-Fi,
or rebooted; recovery-portal auto-sleep does not apply. Join its network and
open <http://192.168.4.1> in a browser. Use the configured portal login when
Basic Authentication is enabled. Captive browsers may require opening that
address in your normal browser.

The existing AP password is shared by recovery and action-enabled APs and takes
effect at the next AP start. Empty deliberately means an open network. Portal
login is separate from the password needed to join Wi-Fi; set both when needed.
An open AP with portal login disabled lets anyone in radio range change device
configuration.
The AP does not require an existing router or internet connection.

Commands do not change the saved operating mode. Reboot returns an Offline
device to Wi-Fi-off operation. Duplicate requests are harmless; conflicting
requests, OTA, or configuration writes can reject a command without changing
the active transport.

Connect acknowledges a request, not association or HA/MQTT readiness. It does
not pause the rest of an action list until connected. It scans for the strongest
AP matching the saved network name, using the boot connection's selection rule.
If scanning fails or finds no match, the Wi-Fi stack selects the AP instead.
Firmware using coprocessor-managed Wi-Fi keeps its normal automatic selection.
The connection timeout starts after radio startup and scanning, so allow extra
time for that preparation. Existing actions using
`wifi_reconnect` must be updated to `wifi_connect`; no alias is retained.

For local status labels, use:

```text
Wi-Fi: [health:wifi_state]
Network: [health:wifi_ssid|off] [health:wifi_ip|]
Signal: [health:wifi_rssi|n/a] dBm
AP purpose: [health:wifi_ap_purpose|off]
Last Wi-Fi error: [health:wifi_last_error]
```

`wifi_state` covers the whole Wi-Fi lifecycle: `OFF`, `STARTING`, `CONNECTING`,
`CONNECTED`, `RECONNECTING`, `AP`, `STOPPING`, and `ERROR`. A failed connection
followed by successful shutdown returns `OFF`; `ERROR` means shutdown failed,
so Wi-Fi-off cannot be confirmed.

`wifi_ssid` and `wifi_ip` describe the active network or AP. Signal strength is
available only while connected to a network. Unavailable details use the normal
binding fallback. `wifi_hostname` remains the hostname configured at boot even
with Wi-Fi off; saved name changes apply to this binding after reboot.
`wifi_connected` is `ON` for a network connection, not for AP;
`wifi_ap_active` identifies AP operation and `wifi_ap_purpose` distinguishes
`INTERACTIVE` from `RECOVERY`.

`wifi_last_error` retains network, AP, or radio failures until successful
connection, AP startup, or reboot. A successful intentional disconnect does not
create an error. Health bindings refresh up to every two seconds, so brief
transitions may not appear. `CONNECTED` means association with an IP address,
not internet or service readiness.

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

The recovery AP retains its restricted provisioning portal without requiring
the normal portal login. After initial setup,
it cannot change an existing portal's Basic Authentication settings. Make those
changes through the authenticated portal on your normal network.
An action-enabled AP exposes the full portal and uses the same configured login
policy as station access, including authenticated changes to these settings.

## Understand saved credentials

Home Wi-Fi passwords, tokens, and API keys are write-only. **Saved on device**
means a value exists; the portal does not display it. An empty field keeps the
stored value, while entering a new value replaces it on Save. Empty does not
mean "clear this credential".

The AP password is the exception: it is ordinary shareable
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
