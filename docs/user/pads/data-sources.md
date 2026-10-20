---
title: Choose live data sources
description: Read MQTT fields, local sensors, health, time, and network activity without maintaining a separate key catalog.
---

Live data can come from MQTT messages, local sensors, device health, the clock,
or other supported features. Each source has its own binding scheme, so choose
the source that represents the value you need rather than assuming every
reading comes from MQTT.

Use **fx > Binding reference** for the exact schemes and keys available on the
running device. It includes descriptions, parameters, and examples.
Local bindings do not require MQTT.

## Read MQTT payloads

Use `[mqtt:topic;path;format]`. Only topic is required.
Empty path or `.` selects the raw payload; a dot-separated object path selects
JSON data. For `{"room":{"temp":22.5}}` on `house/status`:

```text
[mqtt:house/status;room.temp;%.1f]
```

JSONPath `$` prefixes and array indexing are not supported.
Keep payloads small and select a field instead of displaying whole JSON.
The device collects subscriptions from configured pad bindings, including
nested expressions. Multiple consumers can share a topic.
Check publishers and retained state through [MQTT setup](../integrations/mqtt.md);
syntax validation does not test delivery.

## Read local sensors and health

Use `[sensor:key;format|fallback]`, for example:

```text
[sensor:temperature;%.1f|N/A]
[health:cpu;%.0f]
[health:rssi]
[health:ip]
```

Sensor keys depend on registered hardware. Boolean sensor values are 0/1.
Failed samples can retain last-good data until reboot; a displayed number is not
proof of a fresh sample. `[audio:input.rms]`, `input.peak`, and `input.active`
report microphone level/activity on capable firmware; visible binding demand
starts sampling. Music analysis is a [separate source](../media/audio.md).

Health describes this device, not a remote server. For structured health rows,
use `[health:table]` or `[health:extended_table]` with the
[Table widget](../widgets/table.md).

## Read time

Use `[time:format;timezone]`; omitted timezone uses the device setting.
Explicit supported Olson names or POSIX rules remain independent.
Configure synchronization/timezone under [device time](../device/time.md).

Common formats are `%H:%M`, `%I:%M %p`, `%H:%M:%S`, `%Y-%m-%d`,
`%a`/`%A` for weekday, and `%b`/`%B` for month.
`%ms`, `%cs`, and `%ds` append millisecond, centisecond, and decisecond digits.
`%ums` is uptime in milliseconds and does not need NTP.
Wall-clock values can show `--:--` before synchronization.

## Indicate network activity

Use `[net:channel]` for brief 1/0 activity, or `[net:channel;age]` for
milliseconds since activity. Age caps at 999999, also meaning never.
Available channels include portal, mcp, mqtt_rx, mqtt_tx, mqtt, http, ble, ota,
and any where supported. This is activity, not connection readiness.

```text
[expr:[net:any]?"#22c55e":"#334155"]
```

## Choose feature-specific data

Use [alarms](../device/alarms.md), [timers](../device/timers.md),
[camera](../media/camera.md), [audio](../media/audio.md), and
[specialized workflows](../specialized/README.md) for their data semantics.
Use a plain numeric binding for a gauge/bar/sparkline input and format its label
separately. A string with units is not a numeric widget source.

[Pad tasks](README.md) | [User guides](../README.md)
