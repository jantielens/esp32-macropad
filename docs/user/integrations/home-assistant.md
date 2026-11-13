---
title: Set up Home Assistant
description: Choose MQTT, direct HA, or BTHome Bluetooth integration, display live state, and configure actions.
---

Home Assistant integration works in both directions: HA can monitor or control
the device, and device actions can call HA services or broadcast Bluetooth
button events. MQTT, direct HA access, and BTHome Bluetooth are separate
connection paths. Configure only the ones your workflow uses.

## Choose a connection path

MQTT supports discovery, device controls, telemetry, and MQTT bindings.
It needs a [broker](mqtt.md) and HA's MQTT integration, not an HA access token.

Direct live bindings, service actions, and Sparkline history share HA URL/token settings.
They do not require MQTT. Either path can be used independently.
Discovery does not enable service calls, and saving an HA token does not create
an MQTT broker.

BTHome broadcasts button events over Bluetooth. It needs HA's Bluetooth and
BTHome integrations, but no MQTT broker, HA URL/token, or BLE keyboard pairing.
Sensor telemetry can keep using MQTT.

## Send a BTHome button event

On BLE-capable firmware, choose **Connectivity > Send BTHome event** in a
button's action dropdown. This sends a logical remote-control event; it does
not call an HA service directly.

Open the portal's **Connectivity > Bluetooth** section. **Bluetooth Transmit
Power** is shared by BTHome button events, Bluetooth keyboard/mouse/gamepad
HID, and sensor duty-cycle telemetry. It defaults to **+9 dBm**; choose -12
to +9 dBm in 3 dBm steps. Higher power can improve reception but uses more
energy. Choose **Save Transmit Power**, then reboot to apply it. Saving alone
does not change the running radio. Hosted Bluetooth backends leave power
under co-processor control and disable this selector.

In **BTHome Button Event Publishing**, burst duration defaults to **350 ms**
and accepts 100-1000 ms.
Under **Advanced**, advertising interval defaults to **100 ms** and accepts
20-300 ms. Duration must be at least three intervals. Longer bursts add
reception opportunities but delay queued events; neither duration nor interval
guarantees a packet count or delivery.

Choose **Save Event Settings** to apply timing to the next event burst without
reboot. A burst already running retains its original timing. These timing
controls do not change sensor duty-cycle telemetry or HID. Each section saves
only its own controls.

1. Select **Button number** from **Button 1** through **Button 10**.
   Numbers are device-wide, not pad/grid positions. Reusing the same number
   and event on another pad deliberately triggers the same HA automation.
2. Select **Event**: **Press**, **Double press**, or **Long press**.
   The configured event is sent immediately when the action runs. Selecting
   Long press does not require physically holding the button; put the action
   in the pad button's Long-press list if you want that gesture.
3. Choose **Save Pad**. No reboot is required for action changes.
4. Ensure HA has a working Bluetooth adapter or Bluetooth proxy in range.
   Activate the action, then add the discovered **BTHome** device under
   **Settings > Devices & services**.
5. Activate each logical button number you intend to use so HA discovers its
   events. Create an automation with that device as the trigger, selecting the
   matching button number and event. HA can list additional standard events;
   this firmware sends only the three offered above.
6. For example, match **Button 4 / Long press**, choose a harmless action such
   as activating a scene, save the automation, and activate the pad action again.

The ten button numbers and three event types give 30 combinations. Button
numbering stays stable regardless of which pad is active. The action works
with keyboard transport Off, USB, or BLE. BLE keyboard advertising pauses
briefly for each event burst; an established HID connection stays connected.

Events use short, unencrypted, repeated advertisements with packet IDs for
duplicate filtering. They are best-effort: a queued or advertised event is not
confirmation that HA received or executed it. Nearby devices can observe or
spoof broadcasts, so do not use them for security-sensitive operations.
Four events can wait behind the active burst. A full queue rejects the newest
action, logs a warning, and stops its remaining action list. OTA cancels
pending event work; rejected or cancelled events are not retried.

## Discover the device

Connect both the device and HA to the same broker. In HA, open
**Settings > Devices & services > MQTT** and find the device.
Available entities reflect actual firmware capabilities, not a universal list.
Use the discovered entity IDs in automations rather than assuming their names.
Read [remote control semantics](home-assistant-control.md) before testing commands.

For HA data on a pad, explicitly publish it to MQTT through an automation or
HA's MQTT Statestream integration. For example, publishing raw temperature to
`house/living_room/temperature` can be read as:

```text
[mqtt:house/living_room/temperature;;%.1f C]
```

Your publisher owns the topic and payload contract.
Do not assume HA entity IDs are MQTT topics.

## Configure direct HA access

Open **Connectivity > Home Assistant**.
Enter the base URL including scheme/port, such as `http://192.168.1.50:8123`,
and a long-lived access token created in the HA user profile's Security area.
A successful Save applies HA client URL/token settings safely without reboot.
Blank token input preserves the saved token; it cannot be retrieved by export.
An empty URL disables direct HA access.

> [!WARNING]
> Use a trusted network and a suitably restricted HA account. HTTP exposes the
> bearer credential and entity data on the network in plaintext. Live bindings
> support HTTP/plain WebSocket only; HTTPS is rejected, never downgraded.
> REST actions and history retain their existing HTTPS behavior (server
> certificates are not verified). Do not expose this path as an internet endpoint.

## Display live entity state

On firmware with live HA support, **Connectivity > Home Assistant** shows a
**Live Entity State** card with connection state, entity count, counters, and
the last error. The card is hidden on unsupported firmware. A configured
connection is not a guarantee that a particular entity exists or is readable.

Use entity IDs directly in Pad Editor bindings, or use the HA binding builder
described below. To create a complete value button or a toggle button for a light
or switch at an empty position, use the
[Button Builder](../pads/editing.md#create-a-button-from-a-source) with the
Home Assistant provider.
To display a temperature with a fallback while unavailable:

```text
[ha:sensor.living_room_temperature;;%.1f C|---]
```

The syntax is `[ha:entity_id;path;format]`. An empty path or `.` reads the state.
Explicit `state` and dotted paths read from the entity's state document:

```text
[ha:light.living_room;state;|---]
[ha:light.living_room;attributes.brightness;%.0f|---]
```

Formatting and pipe fallbacks use the same rules as MQTT bindings. HA states
`unknown` and `unavailable` are treated as unavailable, so the fallback is shown
instead of an ordinary value. Removed entities and missing/disconnected data
are also unavailable. A connected entity's value does not expire merely
because it has not changed.

Live access requires PSRAM and supports at most 64 referenced entities, a
2048-byte serialized snapshot per entity (including its terminating NUL), and
full reassembled messages up to 15 KiB. Large
attribute sets can exceed these bounds even with few entities. Inspect the
card's error before assuming a blank value is valid; verify the entity ID,
token permissions, and local HTTP URL. The connection pauses during competing
OTA activity and reconnects after transient network failures.

References in all saved pads count toward the limit, including named bindings
and nested expression bindings, not just the pad currently on screen. Preview
references expire after 60 seconds unless referenced again. Resources are
allocated only when HA is configured and an entity is referenced.

This path needs neither MQTT Statestream nor discovery. Existing `[mqtt:...]`
bindings, broker configuration, discovery, and remote controls are unchanged.
MQTT-specific screensaver wake and camera-trigger sources are not automatically
extended to HA live bindings.
REST actions still send commands; live bindings read current state, not Recorder
history. For historical data, use Sparkline backfill below.

## Build an HA binding

The portal's [shared binding workflow](../pads/bindings.md#build-a-token-from-a-source)
includes a **Home Assistant** source in the Binding Builder.
HA lookup generates a token without saving a pad or subscribing to additional
live entities. Both builders use the value acquired during lookup for their
preview; there is no separate device-preview action.

The wand button beside **fx** on bindable fields opens a fresh Binding Builder.
Choose **Home Assistant** to look up an entity and insert into that field.
The source picker explains missing setup; unsupported integrations are hidden.
An **idle** live-binding status is normal when no entities are subscribed.
Lookup works independently; saved pads and temporary previews subscribe
automatically when they reference HA entities.

1. Choose the wand button beside the bindable field you want to fill, then
   select **Home Assistant**.
2. Type at least three characters of an entity ID or
   friendly name and select a match. Search shows up to 20 matches; keep typing
   to narrow the list. Alternatively, enter an exact ID and press Enter.
3. State is selected by default. Expand **Change value** to select an attribute
   or enter a custom dotted path. Use **Display options** to change the
   suggested presentation, unit text, or unavailable-value fallback.
   The entity's reported unit is suggested for its state, not its attributes.
   Widget data bindings and other raw-value widget fields, such as gauge limits,
   automatically omit formatting and units.
   For display text, choose **Number** with 0 to 6 decimal places, or **Text**.
   Edit **Unit (optional)**;
   spacing is automatic. Expand **When unavailable** to set fallback text.
4. Check **Sample value** and the generated binding.
   Use **Insert binding** to fill the originating field, or **Copy binding**
   when authoring elsewhere. Save your changes using the editor's normal Save control.

The **fx** entry opens Binding Reference on **Basics** each time; the wand
opens a new builder. Close either dialog before using the other field control.
Use **Edit value** to select a different source. Selecting an entity fetches its
details automatically; a failed lookup offers Retry. Manual binding entry and
the `ha` syntax reference remain available without a successful lookup.

Entity search uses HA's template API and requires an administrator user's
token. A non-admin token can still load readable entities by exact ID, generate
bindings, and supply normal live HA values. Search permission failures explain
this distinction; invalid credentials and connection failures are reported
separately. Manual binding generation remains available even when lookup fails.

The unit is suggested for the entity's state and cleared when selecting an
attribute. Units are display text, not conversion: adding `kW` does not divide a
value in watts. Percent suffixes are escaped automatically. Samples reflect the
fetched snapshot, not continuous live updates, and may be truncated. Attribute
suggestions include up to 32 scalar dotted paths, not array indices.
Large entity responses can exceed the lookup limit; enter the ID and path
manually in that case. Successful lookup does not waive the live snapshot limits
above.

## Add a service action

In a supported action host, choose **Call Home Assistant service**.
Enter a domain-qualified Entity ID, a bare Service name, and optional
Service Data containing a JSON object:

| Goal           | Entity ID           | Service      | Service Data            |
| -------------- | ------------------- | ------------ | ----------------------- |
| Toggle light   | `light.kitchen`     | `toggle`     | Empty                   |
| Set brightness | `light.kitchen`     | `turn_on`    | `{"brightness_pct":75}` |
| Run scene      | `scene.movie_night` | `turn_on`    | Empty                   |
| Open cover     | `cover.garage`      | `open_cover` | Empty                   |

The domain comes from the entity prefix. Service names use lowercase letters,
digits, and underscores, not `light.turn_on`.
These fields are literal: entity, service, and JSON do not resolve bindings.
Do not insert `[mqtt:...]` and expect a live value.
Save the host/pad, test a harmless call, and inspect the HA result/log.

Requests can be queued; a displayed button response is not a remote completion
receipt. MCP can track eligible calls with
[execution-result lookup](ai-assistants.md#verify-effects).
Confirmation/continuation paths have different tracking limits.
For history access, follow [Sparkline backfill](../widgets/sparkline.md#backfill-from-home-assistant).

[Integration tasks](README.md) | [User guides](../README.md)
