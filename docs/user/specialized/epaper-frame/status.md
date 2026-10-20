---
title: Diagnose Frame refresh
description: Interpret last-refresh counters, outcome and timing, retained state, and delayed wake events without mistaking them for live status.
---

Frame status records refresh outcomes, timing, and delivery information so you
can distinguish an intentional skip from a fetch or panel failure. Because the
device sleeps and can refresh offline, a reported value is not necessarily a
live view of the physical panel.

Open **E-Paper Frame > Status** while the portal is awake.
It polls every five seconds and offers Refresh e-paper now.
Check source URL/format, network, power, and
[wake budget](schedule-and-power.md) before resetting settings.

## Read the result

| Result       | Meaning                                                    |
| ------------ | ---------------------------------------------------------- |
| updated      | Panel image updated                                        |
| skipped      | No update needed, such as an unchanged image               |
| disabled     | Timer refresh suppressed by schedule                       |
| fetch_failed | Content/network path failed before a valid image was drawn |
| draw_failed  | Rendering/panel update failed                              |

Successful refresh count measures panel updates, not every wake.
Last refresh is updated after successful draw with valid time and survives deep
sleep, not full power loss/hard reset.
An unknown timestamp is not necessarily a failed image.
Sidecar HTTP 0 means no usable sidecar response/N/A, not status 200.

Boot/button wakes can show splash feedback. Configuration mode shows connection
details. Error screens explain retry; very low battery can show a warning and
sleep instead of attempting another network cycle.
Use [board power notes](../../hardware.md) if that repeats.

## Compare timings

Status separates boot-to-Wi-Fi, CRC-to-draw, draw-to-MQTT, and active loop time.
Panel/controller wait counters, when present, help distinguish network failure
from a panel-ready timeout.
Inspect the physical panel: a successful image download alone does not prove
draw completion.

## Understand MQTT reporting

With [MQTT configured](../../integrations/mqtt.md), retained
`<base>/epaper/state` represents the latest delivered summary.
It can be older than the physical image after offline cached wakes.
Offline-cycle counts describe the last report interval, not lifetime totals.

`<base>/epaper/wake` carries non-retained per-wake events when delivery is possible.
A small RTC journal queues up to 16 records and replays oldest first at the next
online wake. Power loss can lose undelivered records.
Broker acknowledgement is not an exactly-once archive guarantee: deduplicate
using topic plus session_id and wake_id, and inspect dropped-record counts.

Current wake timing is captured before final telemetry delivery completes.
previous_delivery can describe the final delivery timing of the preceding
reported wake, one wake later. Do not compare those as identical timing windows.
For a practical archive, use [Frame wake events](../../examples/frame-wake-archive.md).

[Frame setup](README.md) | [Image sources](image-sources.md) | [User guides](../../README.md)
