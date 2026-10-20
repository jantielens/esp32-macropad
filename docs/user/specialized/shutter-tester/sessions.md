---
title: Capture and manage shutter sessions
description: Run freeform/guided sessions, author complete test scripts, configure save hooks, and export verified results.
---

A session groups repeated captures with camera/notes and comparison targets.
Freeform sessions collect the shots you choose, while guided tests follow a
script of target speeds and repetition counts.

Complete [calibration](README.md) first.
Use Shutter actions advertised by the device to start/stop a freeform session
or start a guided test. Guided mode sets targets as it progresses; follow the
prompt, use Skip/Redo deliberately, and stop when complete.
Starting capture exits alignment.

## Write a guided test

In the portal's test-script section, save text such as:

```text
# Repeated short series
name: basic|Basic speed check
shots_per_speed: 3
1/60
1/250
1/1000

name: slow|Slow speed check
shots_per_speed: 2
1
1/2
1/4
```

`name: id|Display Name` begins each new script; a blank line alone does not.
Comments start with `#`. Default shots_per_speed is 1; accepted counts clamp
to 1-255. Speed lines must be supported standard speeds, optionally ending in
`s`. Unknown keys/invalid speed lines can be skipped rather than rejecting the
whole file; inspect the parsed list after Save.

At most 16 scripts and 32 speed entries per script are retained.
Keep IDs within 16 bytes, names within 63, and speed text within 15.
Repeated IDs replace earlier definitions.
A List using provider `shutter_tests` can start `guide_start` with
`[list:shutter_tests.selected]` from that List's action.

## Verify saving

Stopping snapshots the session for asynchronous persistence.
Wait, then open Sessions and confirm the expected camera, shot count, targets,
and waveforms. A saving/completion notification alone is not a durable-write
receipt. Use Print/Save PDF in session detail for an external report and check
its content.

Session detail offers notes, charts, speed summaries, Print/Save PDF, and
Copy for AI Analysis. Review copied data for private notes before sharing it.
Use [results interpretation](results.md) before accepting automated diagnosis.
Delete all sessions is irreversible; keep external exports first.

## Configure save lifecycle feedback

Session Save Actions can hold up to three actions at save start and completion.
Use simple feedback such as Notify, a screen change, or an MQTT status message.
These hooks dispatch their entries individually, not as a general pausable
confirmation/delay workflow.
The completion hook means persistence processing returned, not proof every
write succeeded. Inspect Sessions/logs for the result.

Direct session start/stop/toggle actions are guarded against self-triggering;
avoid indirect loops through MQTT or other automation as well.

[Shutter setup](README.md) | [Results](results.md) | [User guides](../../README.md)
