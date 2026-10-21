---
title: Connect MQTT and live data
description: Configure a broker, verify telemetry, publish commands, subscribe through bindings, and use retention deliberately.
---

MQTT exchanges messages through a broker between the device and other services.
The device can publish readings or commands and receive data for live labels,
widgets, and automation triggers. Publishers and consumers agree on topic names
and payloads, rather than needing a direct connection to each other.

## Configure the broker

1. Open **Connectivity > MQTT**.
2. Enter the broker hostname/IP, port (normally 1883), and credentials.
3. Save MQTT Settings and reboot for connection-setting changes.
4. Check MQTT status in the header/health view and inspect the broker's messages.

Saved passwords are write-only; blank preserves the current one.
Portal login is not broker authentication.
The firmware's MQTT client uses a plain connection; a TLS-only broker port is
not enabled by changing its port number. Keep it on a trusted network.

Publish Behavior sets interval and payload scope.
In Always-On, zero disables periodic publishes; other intervals schedule them.
Duty-cycle firmware publishes once per wake instead.
Scope chooses sensors, diagnostics, or both.
See [sensor-node modes](../specialized/sensor-node.md).

## Inspect the device's topics

The device base is `devices/<sanitized-device-name>`.
Use the running name or inspect discovery rather than copying another device's
base. Health state is a JSON payload under `<base>/health/state`.
Capabilities publish additional topics, including retained feature state.
Renaming a device can change the topics external automations use.

For Home Assistant, enable its MQTT integration on the same broker and use
[discovery/setup](home-assistant.md). Device discovery does not automatically
publish every unrelated HA entity for the device to read.

## Feed labels and widgets

Publish a small JSON message, for example `{"temperature":22.5}` on
`house/living_room`, then set a button label to:

```text
[mqtt:house/living_room;temperature;%.1f C]
```

For a numeric widget, omit formatting/units.
To generate a single token, use **fx > Binding builders > MQTT** in a bindable
field. Enter one exact topic and choose **Read topic**, then click its received
raw payload or a scalar field. Scalar payloads are selected automatically.
See the [shared binding workflow](../pads/bindings.md#build-a-token-from-a-source)
for limits, formatting, and temporary inspection cleanup. Inspection does not
save a topic or send a command; a cached sample is not proof of a fresh message.
To generate a complete button at an empty canvas position, use the
[Button Builder](../pads/editing.md#create-a-button-from-a-source) with the
MQTT provider. It requires a received message and supports display scenarios,
not command-topic or toggle creation.
The device subscribes to configured binding topics; use
[data-source syntax](../pads/data-sources.md#read-mqtt-payloads) for paths,
formatting, and [fallbacks](../pads/bindings.md#set-a-fallback).
Reconnection can require messages to arrive again; retained state helps
repopulate subscriptions.

## Publish an action

Add **Publish MQTT message** with a topic and payload, for example
`zigbee2mqtt/kitchen_light/set` and `{"state":"TOGGLE"}`.
Use the destination integration's actual command contract.
Topic/payload fields support bindings. Save Pad or the action host, then verify
the receiving service's result, not just the button flash.
Incoming action triggers use [exact topic/payload filters](../device/triggers.md#mqtt-triggers).

Retain state intentionally, not transient commands such as toggle, reboot,
sound, or an acknowledgement. A retained command can execute again when a
subscriber reconnects. Avoid publish/trigger feedback loops.
Use broker permissions to restrict clients and redact private payloads in
shared logs/exports.

[Integration tasks](README.md) | [User guides](../README.md)
