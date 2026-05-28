#pragma once

#include "board_config.h"

#if HAS_DISPLAY && IS_SHUTTER_TESTER

#include <stddef.h>

// Resolve an alignment sub-domain binding key (stripped of "align." prefix).
// Called by the shutter binding prefix router.
// Returns true if the key was recognized, false otherwise.
bool shutter_align_binding_resolve(const char* key, char* out, size_t out_len);

#endif // HAS_DISPLAY && IS_SHUTTER_TESTER
