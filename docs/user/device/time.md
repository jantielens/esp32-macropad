---
title: Device time and timezone
description: Select local time rules, verify NTP synchronization, and understand their effect on alarms and bindings.
---

The device clock supplies timestamps and time-based features such as clock
labels and alarms. Network synchronization establishes the time; timezone
settings determine the local time those features use.

## Set local time

1. Open **Device > Timezone**.
2. Select a nearby city with the same time rules.
3. Check the **Device time**, UTC offset, and synchronization status.
4. Select **Save** to persist and apply the timezone. No reboot is needed.

Selecting a city only previews the result. It does not save or change alarm
behavior until Save succeeds. The portal uses device time, not the browser's
timezone.

## Use a custom rule

Choose **Custom** to enter a supported Olson name, such as `Europe/Amsterdam`,
or an explicit POSIX rule. The supported city/name catalog is finite; not
every timezone name installed on your computer is accepted.
Use the preview to confirm that the rule is recognized.

For example:

```text
UTC0
CET-1CEST,M3.5.0,M10.5.0/3
```

POSIX offset signs are reversed from the usual "UTC plus/minus" notation:
`CET-1` means UTC+1. The example's transition rules apply European daylight
saving time. Prefer the city selector unless you need custom rules.

## Check synchronization

Wait for **Synchronized** after the device joins a network with working DNS
and access to its NTP time service. If it remains waiting, check Wi-Fi,
gateway/DNS, and network restrictions before configuring time-dependent tasks.

Alarms require an NTP synchronization during the current boot. After that
sync, losing Wi-Fi does not immediately stop the running clock. Reboot starts
a new readiness check. [Camera-roll snapshots](../media/camera.md#save-and-manage-snapshots)
also require valid time to form dated folders.

## Time on a pad

Use a label binding for the configured device timezone:

```text
[time:%H:%M]
[time:%Y-%m-%d]
```

An explicit timezone parameter makes a separate world clock:

```text
[time:%H:%M;Europe/Amsterdam]
```

`%H` is a 24-hour hour, `%M` minutes, `%S` seconds, `%Y` year, `%m` month,
and `%d` day. Use the pad editor's Binding reference for supported formats
and timezone parameters. Save the pad to apply its label changes.

## Alarm and daylight-saving effects

Saving a substantive timezone change dismisses an active alarm using its old
stop actions and rearms scheduling from the next full minute. An already
scheduled one-shot keeps its absolute instant, so its displayed local time
can change. An identical save does not dismiss an active session.

Weekly alarms skip nonexistent local times at a daylight-saving jump and ring
only on the first occurrence of a repeated local time. Review
[alarm lateness and reboot behavior](alarms.md#missed-alarms-and-reboots)
before relying on a schedule.

[Device tasks](README.md) | [User guides](../README.md)
