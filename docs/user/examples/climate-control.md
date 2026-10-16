---
title: Build a climate setpoint control
description: Connect a numeric rocker to a bounded HA temperature adjustment and display confirmed setpoint feedback.
---

A climate-control rocker requests small or large changes to a thermostat's
setpoint while a label shows its confirmed setting. MQTT carries the adjustment
requests to Home Assistant, which applies the chosen bounds.

This example needs [MQTT and Home Assistant](../integrations/home-assistant.md).
Use a real climate entity and its temperature units/allowed range.
It is a convenience control, not a safety thermostat.

## Handle step requests in HA

Create an automation with this YAML, replacing `climate.living_room` and the
16-26 bounds with the actual entity's supported safe range:

```yaml
alias: Pad climate step
triggers:
  - trigger: mqtt
    topic: house/climate/living_room/step
conditions:
  - condition: template
    value_template: "{{ state_attr('climate.living_room', 'temperature') is number }}"
actions:
  - action: climate.set_temperature
    target:
      entity_id: climate.living_room
    data:
      temperature: >-
        {% set current = state_attr('climate.living_room', 'temperature') | float %}
        {% set requested = current + (trigger.payload_json.delta | float(0)) %}
        {{ [26, [16, requested] | max] | min }}
mode: queued
```

Do not retain step commands. Test with a small signed delta before connecting
the pad. HA/device permissions determine whether the request succeeds.

## Build the rocker

Choose Numeric Rocker, Small Step 0.5/Large Step 2, and MQTT Adjustment Action:

```text
Topic: house/climate/living_room/step
Payload: {"delta":{step}}
```

Use supported step sizes; a whole-degree-only target needs different values.
Save Pad and test both signs and the bounds.
Do not put this template in a direct HA service action: those fields are literal.

## Show confirmed state

Publish the HA entity's confirmed temperature setpoint as a raw number on
`house/climate/living_room/setpoint`, through a state automation or configured
MQTT publisher. Then use:

```text
Set [mqtt:house/climate/living_room/setpoint;;%.1f] C
```

Adapt the unit label if needed. Display current measured temperature separately.
Feedback must come from the actual entity state, not merely the requested delta.
Use an unknown fallback while no state has arrived.
Repeated taps can outrun confirmation; verify the final entity state.

[Numeric Rocker](../widgets/rockers.md) | [Examples](README.md) | [User guides](../README.md)
