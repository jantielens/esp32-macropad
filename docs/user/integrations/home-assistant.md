---
title: Set up Home Assistant
description: Choose independent MQTT discovery and REST service paths, supply HA state to pads, and configure literal service data.
---

Home Assistant integration works in both directions: HA can monitor or control
the device, and device actions can call HA services. MQTT and direct HA access
are separate connection paths, so you only need to configure the ones your
workflow uses.

## Choose a connection path

MQTT supports discovery, device controls, telemetry, and MQTT bindings.
It needs a [broker](mqtt.md) and HA's MQTT integration, not an HA access token.

Direct service actions and Sparkline history use HA URL/token settings.
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

## Configure service access

Open **Connectivity > Home Assistant**.
Enter the base URL including scheme/port, such as `http://192.168.1.50:8123`,
and a long-lived access token created in the HA user profile's Security area.
Save to apply without reboot.
Blank token input preserves the saved token; it cannot be retrieved by export.
An empty URL disables service actions.

> [!WARNING]
> Use a trusted network and a suitably restricted HA account. HTTP exposes the
> bearer credential on the network. HTTPS is supported, but the device does not
> verify the server certificate. Do not expose this path as an internet endpoint.

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
