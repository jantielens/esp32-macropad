---
description: "Use when changing OTA, persistent filesystem mutations, or background work that may compete with OTA flash writes. Covers activity ownership and safe checkpoints."
applyTo: "src/app/ota_activity.*, src/app/web_portal_ota*"
---

# OTA Activity Contract

`ota_activity.{h,cpp}` owns the race-safe lifecycle for manual and online
firmware updates. OTA entry points call `ota_activity_try_begin()` before
`Update.begin()` and `ota_activity_finish()` on every recoverable failure.
Successful updates retain ownership through reboot.

Before adding nonessential background network I/O, decoding, rendering,
polling, or PSRAM-heavy computation, decide whether it competes with OTA
flash writes. Check `ota_activity_is_active()` before starting and at an
owned safe point in long-running work. Return, defer, or close the
subsystem's own operation there; do not suspend another task or add a
global pause framework.

Keep WiFi, AsyncTCP, portal responses and OTA status polling, OTA transport,
reboot processing, and safety-critical device-class loops operational.
The image-fetch screen-saver suspension has independent ownership; do not
reuse it for OTA. Add focused coverage for new checkpoints in `tests/` and
exercise them during OTA stress validation.

## Persistent filesystem mutations

When adding filesystem writes, deletions, renames, or directory changes through
`Storage`, use `StorageMutationGuard` from `storage.h` before the first
mutation. Check admission and report or propagate failure. Keep the guard
alive through file close and all related cleanup or rollback. The guard is
active for SD storage with automatic shutdown and is a no-op on other builds;
it reuses the OTA activity writer counter, without adding persistent state.
Protected mutations also defer OTA and network transitions.

For asynchronous uploads, retain ownership from the first mutation until the
file is closed and cleanup completes, including failure and disconnect paths.
If transferring a guard with `release()`, call `storage_mutation_finish()`
exactly once when the owning operation ends.

Voice recording uses a separate shutdown protocol: shutdown first acquires
the activity gate, then cancels the recorder and waits for cleanup. Do not hold
a storage mutation guard for the entire recording, which would prevent
shutdown from reaching cancellation. Preserve this lifecycle when changing
recorder writes.
