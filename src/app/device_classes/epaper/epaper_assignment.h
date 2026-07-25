#pragma once

#include "board_config.h"

#if HAS_EPAPER

#include "device_classes/epaper/epaper_assignment_logic.h"

struct DeviceConfig;
struct EpaperRefreshOutcome;

bool epaper_assignment_load_state(EpaperAssignmentState* state);
void epaper_assignment_accept_state(const EpaperAssignmentState& state);

// Resolve, display when needed, and acknowledge one assignment wake.
EpaperRefreshOutcome epaper_assignment_run(DeviceConfig* config, bool force);

#endif // HAS_EPAPER
