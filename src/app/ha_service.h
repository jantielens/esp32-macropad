#pragma once

#include "board_config.h"

#if HAS_DISPLAY || HAS_BUTTON

#include "pad_config.h"  // HaServicePayload

// ============================================================================
// Home Assistant Service Action — deferred HTTP dispatch
// ============================================================================
// The ha_service button action calls the HA REST API
// (POST /api/services/<domain>/<service>). HTTP I/O must never run on the
// LVGL/UI task, so dispatch is split:
//
//   ha_service_enqueue()  — UI task context; stores the request under a
//                           portMUX spinlock and raises a pending flag.
//   ha_service_execute()  — main loop() context (via action_dispatch_loop);
//                           performs the blocking HTTP POST if pending.
//
// Only one request is held at a time; rapid taps overwrite the pending one.

// Enqueue a HA service call (UI task context, non-blocking).
void ha_service_enqueue(const HaServicePayload& payload);

// Perform a pending HA service call. Call from main loop() context.
void ha_service_execute();

#endif // HAS_DISPLAY || HAS_BUTTON
