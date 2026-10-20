---
title: Build a home-energy pad
description: Publish one power payload, configure paired and signed gauges, and add a comparable trend.
---

A home-energy pad brings consumption, solar production, and grid flow together
on one screen. Gauges show the current balance, while a shared history chart
helps compare how those readings change over time.

## Supply numeric data

Configure [MQTT](../integrations/mqtt.md).
Publish your live values to `house/power`, retaining state if appropriate:

```json
{"house":1200,"solar":2000,"grid":-800,"battery":65}
```

house/solar are nonnegative watts; grid is positive for import and negative
for export. Translate your integration's sign convention before displaying it.
battery is percent. Test initially with this fixture, then connect the real
publisher; a static retained fixture is not live monitoring.

## Name the sources

In Pad Bindings, add:

| Name    | Value                        |
| ------- | ---------------------------- |
| house   | `[mqtt:house/power;house]`   |
| solar   | `[mqtt:house/power;solar]`   |
| grid    | `[mqtt:house/power;grid]`    |
| battery | `[mqtt:house/power;battery]` |

Follow [named binding rules](../pads/bindings.md). Save Pad before relying on
device-side preview of these definitions.

## Compare consumption and production

Add a large Gauge button. Set Ring 1 `[pad:house]`, Ring 2 `[pad:solar]`,
Pair rings 1/2 on, Min -6000/Max 6000, and distinct colors.
Ring 1 is positive and Ring 2 negative; its needle reflects house minus solar.
Set a label `Grid [pad:grid] W`.
This balance is illustrative: batteries/other loads can mean it differs from
the independently metered grid source.

Add a second signed Gauge or Bar Chart with `[pad:grid]`, Min -6000/Max 6000,
Zero-Centered on. For a paired Bar Chart, reverse the pair ordering:
its first slot is negative. See [Gauge](../widgets/gauge.md) and
[Bar Chart](../widgets/bar-chart.md).

## Add history and verify

Add Sparkline with house/solar/grid, 1-hour range, 10-second points, shared
scale, and a zero reference line.
Set battery on a separate 0-100 gauge rather than sharing the watt scale.
Save Pad and inspect fixture values and sign directions.
Publish a second payload with grid positive to verify import/export.

Collection is runtime history and may carry values forward during data gaps.
Use [Sparkline limitations](../widgets/sparkline.md), and add an external
freshness signal if stale energy data would mislead.

[Examples](README.md) | [User guides](../README.md)
