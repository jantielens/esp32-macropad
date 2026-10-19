---
title: Build light controls with feedback
description: Configure a Zigbee2MQTT toggle, state-driven color, and fine/coarse brightness steps with actual device feedback.
---

A light-control pad combines a toggle, brightness adjustments, and feedback
from the light's reported state. The command and feedback paths are separate:
a tap requests a change, while the returned state determines what the pad shows.

Use [MQTT](../integrations/mqtt.md) and an integration accepting the command
contract below. This example uses Zigbee2MQTT's `kitchen_light`; substitute
your supported device and verify its state/brightness features.

## Toggle and show reported state

Add an ordinary button:

| Field                | Value                                                                     |
| -------------------- | ------------------------------------------------------------------------- |
| Center label         | `Kitchen`                                                                 |
| Icon                 | `lightbulb`                                                               |
| Tap action           | Publish MQTT message                                                      |
| Topic                | `zigbee2mqtt/kitchen_light/set`                                           |
| Payload              | `{"state":"TOGGLE"}`                                                      |
| Background color     | `[expr:[mqtt:zigbee2mqtt/kitchen_light;state]=="ON"?"#FFB300":"#374151"]` |

Open the background color swatch to enter the binding in its color dialog.
Save Pad. Tap it and check that the external light changes and publishes state.
The color follows reported state, not optimistic tap state.
A missing message needs a [fallback](../pads/bindings.md#set-a-fallback),
not a claim that the light is OFF.
Do not retain toggle commands.

## Add brightness steps

Add Numeric Rocker with Small Step 10/Large Step 40.
Choose MQTT as its Adjustment Action:

```text
Topic: zigbee2mqtt/kitchen_light/set
Payload: {"brightness_step":{step}}
```

Use `[mqtt:zigbee2mqtt/kitchen_light;brightness]` as a feedback label.
This is the integration's native brightness scale, not automatically percent.
Save and test both directions at the light's limits.

If using direct HA services instead, configure literal `light.turn_on` data
through the [HA action fields](../integrations/home-assistant.md#add-a-service-action).
Those fields do not support `{step}`/binding interpolation; do not copy the
MQTT template into Service Data.

[Rocker setup](../widgets/rockers.md) | [Examples](README.md) | [User guides](../README.md)
