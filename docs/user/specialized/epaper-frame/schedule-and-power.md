---
title: Schedule Frame refresh and power use
description: Configure enabled hours, fixed UTC offset, bounded wake work, retries, and realistic battery estimates.
---

A Frame normally sleeps between image refreshes to reduce power use.
Cadence and enabled hours determine when it refreshes, while wake budgets
limit how long scheduled network work can keep it active.

Configure the source's cadence in
[Images & Schedule](image-sources.md), then choose enabled refresh hours.
Timer wakes during disabled hours retain the image and sleep toward the next
enabled window. Manual/button refresh bypasses the hour gate.

## Set local schedule time

Frame scheduling uses its configured fixed UTC offset, -12 through +14 hours.
It does not use the interactive device's regional timezone/DST rules.
Adjust it seasonally if your location changes clocks.
Initial network time synchronization is needed for meaningful scheduling and
overlay timestamps.

## Bound awake work

The timer-wake maximum budget is 5000-600000 ms, default 15000.
It caps normal scheduled work, not cold-boot/manual configuration work.
Wi-Fi, image fetch, and MQTT have Expected/Stop after controls.
Per-stage stop limits are 500-30000 ms and still fit within the overall cap.
Each request, including prefetch, consumes budget; a physical panel waveform
already in progress cannot be interrupted like a network request.

Changing network timing or offline-batch settings recalculates the portal's
suggested maximum. Edit the maximum last if choosing an explicit override.
Allow time for panel work and clock sync, not only the image download.
Too small a cap can keep the previous image and defer telemetry.

Cutoff retry 0 uses the normal source cadence; a positive value up to 3600
seconds requests an earlier retry. Repeated Wi-Fi failure uses exponential
backoff capped by WiFi Failure Backoff, configurable 60-86400 seconds.
Recovery/configuration can use different behavior from a normal timer wake.

## Estimate, then measure

The Battery Life Estimate is a planning model, not a measured guarantee.
Battery condition, panel power, Wi-Fi signal, leakage, cache misses, manual
refreshes, and retries affect real use.
Source duration, enabled hours, CRC skips, and
[offline batches](image-sources.md#cache-and-reduce-online-wakes) trade freshness
for network/panel work.

Use [Status](status.md) to compare actual wake timings and update outcomes.
An unchanged panel can mean an intentional CRC/disabled-hour skip, not a hang.
An offline frame intentionally will not respond to every remote command.

[Frame setup](README.md) | [Image sources](image-sources.md) | [User guides](../../README.md)
