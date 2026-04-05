#pragma once

#include "board_config.h"

#if HAS_SCALE

// Initialize all scale and brew subsystems.
// Call after LittleFS is mounted and binding_template is ready.
void scale_subsystem_init();

// Initialize brew log storage.
// Call after LittleFS is mounted but before binding init.
void scale_subsystem_init_storage();

#endif // HAS_SCALE
