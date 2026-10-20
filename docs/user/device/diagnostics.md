---
title: Inspect device health and failures
description: Use health, logs, retained crashes, and screen preview to gather useful diagnostic evidence.
---

Diagnostics help you understand the device's current state and investigate
failures. Health readings, logs, screen captures, and crash reports provide
different evidence; a saved report may describe an earlier event rather than
what is happening now.

## Check device health

Select the CPU/health badge in the portal header. Check uptime, current reset
reason, CPU load, temperature, RAM/PSRAM, firmware/storage usage, network
status, and display update rate where available.

Display FPS measures updates, not the panel's physical scanning frequency.
A quiet screen can show zero FPS without a display fault. Some memory
measurements are cached or omitted to avoid disrupting display work.
Use [Storage](storage.md) for a mount failure or capacity problem.

## Read application logs

On firmware that supports remote logs, open **Device > Logs** on your normal
network, not the setup AP. It follows the portal's configured login policy.
The viewer starts with retained startup records, then recent application logs.
Gap markers indicate records that were overwritten or missed.

* Pause stops retrieval; resume when ready.
* Auto-scroll stops following when you scroll upward.
* Clear removes the browser's displayed history, not the device's log buffers.
* Copy or Download exports the history currently retained by the browser,
  including gap markers.

Device buffer capacity and browser history are different. A busy device can
overwrite records before retrieval. Dropped remote-capture records do not
necessarily mean serial logs were dropped.
Reboot and deep sleep clear the in-memory application log buffers.

These are centralized application logs, not ROM boot output or arbitrary
serial writes. If the portal cannot start, use the board's
[serial diagnostic connection](../hardware.md).

## Distinguish a retained crash from this boot

When available, **Retained crash** reports the saved panic, task, exception,
instruction/fault addresses, registers, and crashed firmware hash.
**Crash age unknown** means the dump can be older than this boot or installed
firmware. Successful restarts do not establish that it is a new crash.

Use **Copy crash summary** for the current boot identifiers and retained-crash
details. **Download crash dump** saves the binary when a valid dump exists;
the portal does not erase it. Downloads are unavailable during firmware updates
and are canceled if you leave the view or hide the browser.

A missing dump does not disprove a crash: power loss, corruption, or failure
during capture can prevent it. The dump is not previous text-log history.
Offline decoding requires the exact matching firmware ELF, identified by the
displayed SHA256; provide that identity rather than guessing a build.

> [!WARNING]
> Logs can identify networks, topics, and services. Crash dumps can contain
> private stack data, including credentials. Keep raw downloads private, enable
> portal authentication, and redact sensitive details before sharing evidence.

## Inspect the current screen

Open **Display > Screen Preview**. Choose **Current Screen**, then
**Refresh Preview** to capture the current framebuffer.
Screen selection applies immediately and is not a saved startup preference.
Refresh after the content changes; the preview is not live video.

On supported touch displays, clicking/tapping the captured image queues one
normal device tap and refreshes once afterward. Empty letterbox bars are not
tap targets. The device may wait for a physical touch release or screen wake.
After changing screens, capture a new preview before interacting.
Drag, swipe, long press, and multi-touch are not supported by this control.

A framebuffer capture cannot prove the physical panel updated or powered down.
Compare the device itself when investigating refresh or brightness issues.

## Gather an issue report

Include the exact target, firmware version, reset reason, steps to reproduce,
expected/actual result, and relevant health/log excerpts. Note whether the fault
follows a particular pad, asset, or connected peripheral.
Include the crash summary and matching firmware identity when available.
Share a sanitized pad export only when needed, not an unrestricted config or
raw crash dump in a public report.

[Device tasks](README.md) | [User guides](../README.md)
