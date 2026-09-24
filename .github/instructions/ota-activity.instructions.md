---
description: "Use when changing OTA firmware updates or adding background network I/O, decoding, rendering, polling, or PSRAM-heavy tasks that may compete with OTA flash writes. Covers activity ownership and safe checkpoints."
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
