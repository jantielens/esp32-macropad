#pragma once

#include "board_config.h"

#if HAS_DISPLAY && HAS_PSRAM
// Registers the descriptor only; metadata is loaded on demand by the service.
bool list_source_pads_init();
#endif
