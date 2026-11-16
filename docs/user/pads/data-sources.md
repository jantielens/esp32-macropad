---
title: Choose live data sources
description: Read MQTT fields, local sensors, battery information, health, time, and network activity.
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

In the pad editor, open **fx > Build a binding**, choose **External Data**,
and select a configured source. Enter a JSON path or choose a source statistic.
The preview reads cached JSON values or current source statistics; it does not
fetch the source. Choose display formatting or a fallback, then insert the binding.

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
from 1 to 86400 seconds and default to 1800. The interval starts after a
successful fetch finishes; other work can delay the next attempt. Failed
fetches retry with backoff starting at 60 seconds. Responses are limited to 16 KiB
and each JSON snapshot has a 48 KiB PSRAM document budget. A refresh temporarily
keeps both the current and replacement snapshots. TLS, the response buffer, and
the worker stack use additional memory.

Saved URLs are visible in the portal and configuration API. Without portal
authentication, anyone with access to the device can read them. Avoid using
sensitive tokens or API keys. Edit the JSON to change or rename a source, and
remove an entry to delete it. Changes apply without reboot. HTTP exposes URLs
and data in transit; use HTTPS where possible.
HTTPS requires a synchronized device clock and a certificate trusted by the
firmware's CA bundle. Check the provider's availability and terms before use.

An [MCP assistant](../integrations/ai-assistants.md) can configure a named source
with control permission, inspect cached fields with pad-authoring permission,
and use those fields in pad bindings. Inspection returns bounded pages, not a
new fetch. Source URLs submitted through MCP also pass through the assistant
client and its unencrypted local connection; prefer keyless APIs.

The status shows the age of the last successful refresh, not the age of the
provider's data. Check any provider timestamp and its context before treating
a value as current. A failed refresh
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
[health:wifi_rssi]
[health:wifi_ip]
```

Sensor keys depend on registered hardware. Boolean sensor values are 0/1.
Failed samples can retain last-good data until reboot; a displayed number is not
proof of a fresh sample. `[audio:input.rms]`, `input.peak`, and `input.active`
report microphone level/activity on capable firmware; visible binding demand
starts sampling. Music analysis is a [separate source](../media/audio.md).

Health describes this device, not a remote server. To browse local health
metrics, use `[health:metrics]` as a
[List source](../widgets/list.md#display-device-health).

## Show battery information

Use `[battery:key;format|fallback]` for board-independent battery information.
The same pad can run on battery-telemetry boards with different gauges or
voltage-only monitoring. On these boards, keys remain valid even when the
provider cannot measure them; unsupported or unavailable
readings use your fallback, not a successful empty value or an invented zero.
The live Binding Reference identifies which fields the board supports and
describes its measurement source and calibration limits.

Boards without battery telemetry omit the entire battery scheme from the
Binding Reference. Battery tokens on those boards produce `ERR:unknown`, even
with a fallback; use battery bindings only on boards that list the scheme.

For battery labels, insert these in the button editor and **Save Pad**:

```text
Battery: [battery:percentage;%.0f|N/A]%
[battery:status|Unknown]
Battery draw: [battery:discharge_power;%.2f|N/A] W
Remaining: [battery:time_remaining;%.0f|N/A] min
Until full: [battery:time_to_full;%.0f|N/A] min
```

Numeric fields also work in gauges, bars and sparklines. Use the raw token,
such as `[battery:discharge_power]`, for the widget input; format its label
separately. Status is text, not a numeric widget source.

Current is in amperes and signed power is in watts: positive means charging,
negative means discharging. `discharge_power` is positive battery draw, or zero
when the measured current is not discharging. On external power, neither reading
measures total device consumption. `external_power_present` reports supply
detection separately; it does not prove charging.

Status is Charging, Discharging, Idle or Full only when hardware can establish
it. A non-charging signal does not necessarily mean Idle, and 100% does not
prove charge completion. Voltage-only boards cannot supply current or runtime.

Runtime estimates are in minutes and come from the provider, not a calculation
based only on percentage. `time_remaining` is available only while discharging;
`time_to_full` only while charging. Their accuracy depends on gauge configuration,
calibration and future load. Wi-Fi and display activity can change the estimate.
An unavailable estimate is different from a valid zero-minute estimate.

Measurements expire after three sampling intervals without a successful reading,
excluding observed OTA sampling pauses. Status and runtime estimates clear
immediately when invalid or inapplicable. Power clears when a partial sample
cannot pair new voltage and current; it never multiplies unrelated cached values.
E-Paper Frame samples follow existing wake/refresh operations, not extra polling,
and expire after three minutes of active runtime. Reboot or deep sleep clears
the cache. Unrelated `[sensor:...]` readings retain their own sampling behavior.

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
