---
title: Monitor a server without confusing device health
description: Publish external CPU/memory telemetry, chart numeric values, and keep local-device status clearly labeled.
---

A server-monitor pad turns telemetry from another machine into CPU/memory
gauges and trends. The `health:` scheme describes the macropad itself, so remote
measurements need an external publisher.

Configure [MQTT](../integrations/mqtt.md) and have a server-side collector publish
JSON to `servers/nas/state`:

```json
{"cpu":23,"memory":61,"online":1}
```

Numbers are percentages; online is 1/0. The collector owns sampling, shutdown
state, and stale/offline handling. A retained online value alone cannot detect
a silent server failure; use broker availability or a real freshness contract.

## Add live widgets

Create a Gauge for `[mqtt:servers/nas/state;cpu]`, Min 0/Max 100, labeled
`NAS CPU`. Add a second gauge or Bar Chart for memory with the same scale.
Use numeric inputs without units; put `%` in labels.
Add a Sparkline with CPU/memory, 10-minute window, 5-second points, shared scale.
Save Pad and publish varying fixture values before enabling the collector.

## Separate local diagnostics

Add another clearly named button, `Pad health`, using `[health:cpu]%` and
`[health:rssi] dBm`, or a [health Table](../widgets/table.md).
Do not label a local health sparkline `NAS CPU`.

Disconnect the publisher and inspect what remains displayed.
Both retained MQTT state and Sparkline carry-forward can look healthy while
stale. Show your collector's availability signal with an explicit unknown
fallback, and use an external monitoring system for unattended alerts.

[Data sources](../pads/data-sources.md) | [Examples](README.md) | [User guides](../README.md)
