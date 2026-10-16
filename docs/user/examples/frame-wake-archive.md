---
title: Archive Frame wake events in Home Assistant
description: Append MQTT wake records to a JSONL archive while preserving identity, duplicates, and delayed-delivery caveats.
---

A wake-event archive records Frame refresh and delivery reports outside the
device so you can investigate patterns over time. Home Assistant can append
the MQTT events to a file, preserving their device identity and wake timing.

Configure [Frame MQTT reporting](../specialized/epaper-frame/status.md#understand-mqtt-reporting).
Events arrive on `devices/DEVICE_NAME/epaper/wake`.
Offline cached wakes can be delivered later; receiving time is not capture time.

## Prepare an append target

Set up HA's File integration to append notifications to a writable path.
For example, create `/config/epaper_wakes.jsonl` and select the corresponding
notify entity in the automation. Follow HA's allowed-path/file permissions
requirements.

Do not put private device/network events under `/config/www` unless you intend
them to be web-accessible through `/local`. A private archive is safer.
Rotate/archive the file externally; it does not have unlimited capacity.

## Append topic and event together

```yaml
alias: Archive frame wakes
triggers:
  - trigger: mqtt
    topic: devices/+/epaper/wake
actions:
  - action: notify.send_message
    target:
      entity_id: notify.YOUR_FRAME_ARCHIVE
    data:
      message: >-
        {{ {"topic": trigger.topic, "event": trigger.payload_json} | to_json }}
mode: queued
```

Replace the notify entity, enable the automation, and trigger a manual refresh.
Confirm a JSON object was appended with both topic and event.
The wildcard subscribes to all device names at that topic depth; narrow it for
a single frame.

## Read the archive honestly

Some File configurations add a banner. Keep JSON-object lines when processing:

```bash
grep '^{' epaper_wakes.jsonl | jq -s .
```

Preserve original records, deduplicate analytics by topic/session_id/wake_id,
and inspect reported journal drops.
Full power loss can lose deferred records; replay can produce duplicates.
previous_delivery timing concerns a previous delivery, not necessarily the
current wake. This archive is best-effort telemetry, not an exact wake ledger.

[Frame status](../specialized/epaper-frame/status.md) | [Examples](README.md) | [User guides](../README.md)
