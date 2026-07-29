#pragma once

#include "board_config.h"

#if HAS_DISPLAY || HAS_BUTTON

#include "ha_service_delivery.h"

// ============================================================================
// Home Assistant Service Action — deferred HTTP dispatch
// ============================================================================
// The ha_service button action calls the HA REST API
// (POST /api/services/<domain>/<service>). HTTP I/O must never run on the
// LVGL/UI task, so dispatch is split:
//
//   ha_service_enqueue()  — producer context; appends under a portMUX.
//   ha_service_execute()  — main loop() context (via action_dispatch_loop);
//                           removes and performs at most one HTTP POST.

// Enqueue a HA service call (UI task context, non-blocking).
HaServiceEnqueueResult ha_service_enqueue(const HaServicePayload& payload,
										  uint32_t execution_id = 0,
										  uint8_t action_index = 0);

// Perform a pending HA service call. Call from main loop() context.
void ha_service_execute();

#if HAS_MCP
bool ha_service_execution_reserve(uint8_t action_count, uint32_t* execution_id);
bool ha_service_execution_set_action(uint32_t execution_id, uint8_t result_index,
									 uint8_t action_index,
									 const HaServicePayload& payload);
bool ha_service_execution_record(const HaServiceResult& result);
HaExecutionLookupResult ha_service_execution_snapshot(
	uint32_t execution_id, HaExecutionSnapshot& snapshot);
#endif

#endif // HAS_DISPLAY || HAS_BUTTON
