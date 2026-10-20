---
title: Set up Home Assistant
description: Choose independent MQTT and direct HA paths, display live entity state, and configure service actions.
---

Home Assistant integration works in both directions: HA can monitor or control
the device, and device actions can call HA services. MQTT and direct HA access
are separate connection paths, so you only need to configure the ones your
workflow uses.

## Choose a connection path

MQTT supports discovery, device controls, telemetry, and MQTT bindings.
It needs a [broker](mqtt.md) and HA's MQTT integration, not an HA access token.

Direct live bindings, service actions, and Sparkline history share HA URL/token settings.
They do not require MQTT. Either path can be used independently.
Discovery does not enable service calls, and saving an HA token does not create
an MQTT broker.

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
[Home Assistant Button Builder](../pads/editing.md#create-a-button-from-home-assistant).
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

The portal's **Binding Reference & Builders** dialog includes a **Home Assistant**
view under **Binding builders**. It generates a token without saving a pad or subscribing
to additional live entities.

When live HA bindings are supported and the HA URL and token are saved, an
**HA** shortcut appears beside **fx** on bindable fields. Choose **HA** to open
a fresh Home Assistant builder directly and insert into that field. The shortcut
does not depend on an active connection or administrator permissions.
An **idle** live-binding status is normal when no entities are subscribed.
Lookup works independently; saved pads and temporary previews subscribe
automatically when they reference HA entities.

1. Open **Binding Reference & Builders** from the bindable field you want to fill, then
   select **Binding builders > Home Assistant**.
2. Under **Choose an entity**, type at least three characters of an entity ID or
   friendly name and select a match. Search shows up to 20 matches; keep typing
   to narrow the list. Alternatively, choose **enter an entity ID manually**,
   enter an exact ID, then choose **Use entity** or press Enter.
3. Under **Format the value**, choose **State**, a suggested attribute, or
   **Custom path** to enter a dotted path. These controls appear after selection.
   Numeric states initially select **Number**, one decimal place, and the
   entity's `unit_of_measurement` when it fits the binding syntax. These are
   suggestions: change any field, or choose **As received** for numeric widget
   inputs. Refreshing details preserves your formatting choices.
   For display text, choose
   **Number** with 0 to 6 decimal places, or **Text**. Edit **Unit (optional)**;
   spacing is automatic. Expand **When unavailable** to set fallback text.
4. Under **Insert the binding**, check the generated binding and **Binding result**.
   Use **Insert binding** to fill the originating field, or **Copy binding**
   when authoring elsewhere. Save your changes using the editor's normal Save control.

The **fx** entry opens on **Basics** each time; **HA** opens the builder directly.
Both entries start with an empty builder.
All three sections stay in the same view. Use **Change entity** to select a
different source, or **Refresh details** to fetch a newer sample and attributes.
Selecting an entity fetches its details automatically; a failed fetch does not
prevent formatting or insertion.
Use **full syntax and examples** for the `ha` reference, or
**Open Home Assistant builder** in that reference to return to the builder.

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
