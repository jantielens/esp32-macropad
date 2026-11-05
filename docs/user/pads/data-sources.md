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

## Read external JSON

On supported display firmware, **Connectivity > External Data** fetches named
HTTP or HTTPS JSON sources independently of the visible pad. To add a source,
enter a JSON array and select **Save**. For example, replace the coordinates in
this public weather URL with your location:

```json
[
  {
    "name": "weather",
    "url": "https://api.open-meteo.com/v1/forecast?latitude=51.05&longitude=3.72&current=temperature_2m&daily=temperature_2m_max&timezone=auto&forecast_days=2",
    "refresh_seconds": 1800
  }
]
```

Use the source in a label or widget binding:

```text
[ext:weather;current.temperature_2m;%.1f C|No data]
[ext:weather;daily.temperature_2m_max.1;%.0f C|No data]
```

The dot-separated path selects object keys and zero-based array indices.
Numeric object keys remain keys. An empty path or `.` selects the root.
Objects and arrays are returned as JSON if they fit the binding output.
Literal dots, semicolons, and binding delimiters in key names are unsupported.
Null, missing paths, and values too long for the output use the fallback.
Paths are limited to 47 bytes, formats to 23 bytes, and normal binding values
to 127 bytes.

Up to four sources are supported. Names are case-sensitive and use 1-32 ASCII
letters, digits, underscores, or hyphens. URLs are limited to 511 bytes and
cannot contain credentials in the authority portion. Refresh intervals range
from 60 to 86400 seconds and default to 1800. Responses are limited to 16 KiB
and each source has a 48 KiB PSRAM document budget shared by the current and
replacement snapshots. TLS and the worker stack use additional memory.

Saved URLs are visible in the portal and configuration API. Without portal
authentication, anyone with access to the device can read them. Avoid using
sensitive tokens or API keys. Edit the JSON to change or rename a source, and
remove an entry to delete it. Changes apply without reboot. HTTP exposes URLs
and data in transit; use HTTPS where possible.
HTTPS requires a synchronized device clock and a certificate trusted by the
firmware's CA bundle. Check the provider's availability and terms before use.

The status shows the age of the last successful refresh. A failed refresh
retains the last value and marks it stale. Sparklines continue sampling that
value according to their normal sampling behavior. Removing or renaming a
source, changing its URL, or rebooting clears its cached value. The fallback
is used until the next successful fetch.

Use `.@` paths to show statistics for the source rather than fields in its JSON:

```text
[ext:weather;.@state|Unknown]
[ext:weather;.@age;%.0f s|Unknown]
[ext:weather;.@next;%.0f s|Unknown]
[ext:weather;.@duration;%.0f ms|Unknown]
[ext:weather;.@size;%.0f bytes|Unknown]
[ext:weather;.@http_status|Unknown]
[ext:weather;.@error|No error]
```

`state` is `waiting`, `healthy`, `stale`, or `error`. `age` is seconds since
the last successful refresh; `size` is the decoded response size in bytes from
that refresh. `duration` is the elapsed milliseconds for the last completed
attempt, and `http_status` is its HTTP response code, if received. A failed
attempt retains the previous successful age and size. `next` estimates seconds
until the next attempt; Wi-Fi, OTA, or other sources can delay it. It is
unavailable during a fetch or OTA. Unavailable statistics use the fallback.
Interrupted attempts discarded by OTA or configuration changes do not replace
the last completed attempt. Source statistics reset when the cached value is
cleared.

## Read runtime variables

`[var:name]` reads mutable, device-global text written by a button action. It
is useful for counters, toggles, and other local state that is not a hardware
reading. Variables need no network integration, survive pad changes and
configuration reloads, and clear on reboot. See [runtime variables](bindings.md#read-a-runtime-variable)
and [Set variable](actions.md#set-a-runtime-variable).

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
