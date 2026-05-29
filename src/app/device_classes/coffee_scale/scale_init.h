#pragma once

#include "board_config.h"

#if HAS_SCALE

// Initialize all scale and brew subsystems.
// Called from coffee_scale DeviceClass.on_setup_late (after LittleFS mount,
// after WiFi/AP/portal init, after web_portal_init).
//
// Phase 2: empty body. Phase 3/4 will populate with scale_binding_init() +
// brew_templates_init() + brew_manager_init() + brew_binding_init().
void scale_subsystem_init();

// Initialize brew log storage.
// Called from coffee_scale DeviceClass.on_setup_late before scale_subsystem_init().
//
// Phase 2: empty body. Phase 4 will populate with brew_log_init().
void scale_subsystem_init_storage();

#endif // HAS_SCALE
