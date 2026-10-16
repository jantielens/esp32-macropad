---
title: E-Paper Frame runtime and telemetry
description: Engineering ownership for the sleep-first refresh pipeline, budget accounting, cache admission, and RTC delivery journal.
---

The [Frame user topics](../user/specialized/epaper-frame/README.md) own setup
and interpretation. The [Next Image contract](photoframe-next-image/contract.md)
owns service wire/media requirements.

## Runtime boundaries

Frame uses its dedicated panel path, not LVGL pads, widgets, or interactive
image-fetch state. Board capability gates and driver aggregation remain in
[the device class](../../src/app/device_classes/epaper_frame).
Do not infer source mode from populated URLs: the explicit selector chooses
carousel versus service.

Timer work connects Wi-Fi as needed, resolves/fetches validated content,
composites overlay, completes panel work, attempts bounded telemetry, and sleeps.
Cold/manual recovery paths differ from normal timer budgets and hour gating.
RTC retains wall time and successful image identity across deep sleep, not
across full power loss.
Panel state and a cached image are not automatically proof of a successful
new draw.

## Budget accounting

[Wake budget](../../src/app/device_classes/epaper_frame/epaper_frame_wake_budget.cpp)
passes each interruptible stage the smaller of its cap and remaining overall
budget, retaining shutdown reserve. The image cap applies per manifest/content
request, including batch prefetch. A started physical panel waveform completes
before sleep.

Wake telemetry's budget object separates overall_limit_ms, elapsed_ms,
remaining_ms, stage limit/target values, and cut.
cut is none, wifi_budget, fetch_budget, mqtt_budget, or overall_budget.
Expected targets are tuning measurements, not independent hard deadlines.
The journal preserves cutoff diagnostics for later delivery when the current
wake cannot afford MQTT.

## Content and cache integrity

Carousel sidecars compare exact image CRC but are best-effort; a missing
sidecar proceeds to image fetch. Service admission verifies transport CRC,
declared media/geometry, baseline JPEG constraints, internal G16P payload CRC,
and strict raw-DEFLATE G16Z framing as applicable.
No wrapped/trailing compressed data is admitted.
The service contract is authoritative for header/redirect rules.

[SD cache](../../src/app/device_classes/epaper_frame/epaper_frame_sd_cache.cpp)
stores validated transport blobs, separate from primary configuration storage.
[Offline queue](../../src/app/device_classes/epaper_frame/epaper_frame_offline_queue.cpp)
uses best-effort RTC selection state: online sync selects/prefetches and offline
wakes render without Wi-Fi/MQTT. Cache/source/token/mode changes invalidate it.
Missing or corrupt entries recover online only within the remaining budget.
Storage bus lifecycle must follow the selected board driver, not interactive
image-library assumptions.

## MQTT delivery and RTC journal

[Frame MQTT](../../src/app/device_classes/epaper_frame/epaper_frame_mqtt.cpp)
uses minimal wake connections without the generic LWT/availability/control
subscription pipeline, then clean disconnect.
Retained epaper/state is last delivered aggregate; non-retained epaper/wake
is a correlated event. They are not necessarily current panel state.

Each journal entry carries session_id, wake_id, wake_reason, result, last_stage,
delivery, reported_by_wake_id, battery/network/image diagnostics, and stages_ms.
Record creation precedes Wi-Fi. Interrupted wakes retain the last completed
stage. The RTC journal holds 16 records; oldest overflow is reported through
dropped_wake_records.

Before retiring a record, the device waits for its own matching broker echo,
bounded to 250 ms. Unconfirmed records replay oldest-first on a connected wake;
delivery becomes deferred and records identify the reporting wake.
Aggregate state uses its matching delivery ID for confirmation as well.
Broker-received/echo-lost cases duplicate records. Consumers deduplicate by
device topic, session_id, and wake_id. Full power loss can lose queued records.

wake_loop_ms and active_before_telemetry stop before final MQTT work.
previous_delivery reports the preceding wake's final mqtt_connect_ms,
mqtt_publish_ms, and total_active_ms one wake later. It is absent after RTC
initialization. Do not combine these as one current-wake timing window.

The optional persistent wake log has its own implementation in
[wake log](../../src/app/device_classes/epaper_frame/epaper_frame_wake_log.cpp);
it is separate from the RTC broker-delivery journal.

## Portal component boundaries

epaper-status owns refresh/status component actions.
epaper-vcom owns VCOM read/program/test-pattern actions; preview is volatile,
programming touches limited-endurance EEPROM.
Source/overlay saves preserve the hidden Frame operating mode.
Configuration names and registration belong to
[portal initialization](../../src/app/device_classes/epaper_frame/web/epaper_init.js);
normal fragment saves use the shared deferred-reboot mechanism.

| Method   | Component route                                | Effect                                               |
| -------- | ---------------------------------------------- | ---------------------------------------------------- |
| POST     | `/api/component/epaper-status/refresh`         | Forced refresh attempt                               |
| GET      | `/api/component/epaper-status/status`          | Latest counters, clock status, outcome, and timing   |
| GET      | `/api/component/epaper-vcom/vcom`              | Read programmed bias                                 |
| POST     | `/api/component/epaper-vcom/vcom`              | Program EEPROM                                       |
| POST     | `/api/component/epaper-vcom/vcom-test-pattern` | Draw pattern, optionally with volatile `?vcom=-X.XX` |

Handlers live in the [class components](../../src/app/device_classes/epaper_frame/components)
and use the shared component-action dispatcher/authentication.

Hardware geometry, exact panel bias, physical wake controls, and supported
formats belong to [board documentation](../user/hardware.md).
