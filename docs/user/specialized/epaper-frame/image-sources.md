---
title: Choose Frame image sources
description: Configure URL slots or a supported next-image service, CRC checks, cache, and offline image batches.
---

A Frame displays images prepared elsewhere, so its source determines where
content comes from and which image is selected next. Direct URL slots provide
a configured carousel; a compatible image service can choose images for each
online refresh.

Open **E-Paper Frame > Images & Schedule**.
Use only formats/geometry in the [board reference](../../hardware.md).
A browser-viewable image is not necessarily a supported panel transport.

## Use direct image URLs

Choose Direct image URLs and fill up to five slots.
Each has a URL and Duration/Stay controls; the carousel skips empty slots and
uses configured durations for cadence. Order is top to bottom.
There is no separate global wake-every setting for this mode.
Save and test via Status > Refresh e-paper now.

Optional CRC checking requests `<image-url>.crc32`.
The text can be an eight-digit hexadecimal CRC32, `0x`-prefixed hexadecimal,
or decimal. It represents the exact image bytes.
An unchanged sidecar can skip downloading/redrawing; forced cold boot, button,
or portal refresh bypasses that skip.
A failed/missing sidecar is treated as changed so the image can still be fetched.
Do not diagnose a successful image as failed solely because its sidecar returned
404. CRC checking is off by default.

## Use Photoframe Next Image API

When offered, select the service source and enter the base URL without
`/api/v1/next`, optional bearer token, and refresh interval 1-86400 seconds.
Blank token input keeps the previous token. Save and force a refresh.
The service must support the device's advertised media/geometry.
Compatible servers should follow the
[service contract](../../../dev/photoframe-next-image/contract.md).

The server can provide an image or instruct the frame to keep the current one.
A manual content redirect does not forward the bearer token.
Unauthorized, unsupported, malformed, or unavailable responses retain the old
image and record a failure/recovery result; they do not establish display success.
Selection history at a server is not proof the image reached the panel.

## Cache and reduce online wakes

SD image caching, where offered, stores validated transport blobs.
It is not the interactive [image library](../../media/images.md), and primary
configuration storage remains separate.

Offline refreshes between syncs needs service mode plus SD cache.
Set 0 to keep every scheduled refresh online; 1-16 prefetches that many future
images during an online cycle. It selects the first and queues later candidates.
For example, 5 offline refreshes at 5-minute cadence means roughly one online
sync every 30 minutes, plus work time.

Offline wakes use no Wi-Fi/MQTT, so server selection and telemetry can lag.
The queue is best-effort, survives deep sleep, and is not durable across full
power loss. Button refresh clears it and goes online.
Source/token/mode/cache/batch changes or cache clearing invalidate queued work.
Disabled hours do not consume entries.
Missing/stale/corrupt entries clear the queue and may trigger online recovery
if the wake budget permits.

[Frame setup](README.md) | [Schedule/power](schedule-and-power.md) | [User guides](../../README.md)
