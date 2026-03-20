#pragma once

#include "board_config.h"

#if HAS_SENSOR_HX711

#include "brew_manager.h"

// ============================================================================
// Brew Template Registry
// ============================================================================
// Built-in templates are registered at startup via brew_templates_init().
// Future config-driven templates (loaded from LittleFS JSON) can be added at
// runtime with brew_template_register() — the manager is pointer-agnostic.

// Register a template. Built-ins pass is_dynamic=false (static const storage).
// Dynamic templates (heap-allocated from JSON) pass is_dynamic=true and will
// be freed when brew_templates_clear_dynamic() is called.
void brew_template_register(const BrewTemplate* t);

// Look up a template by name. Returns nullptr if not found.
const BrewTemplate* brew_template_find(const char* name);

// Remove all dynamically-allocated templates (called before reloading from FS).
void brew_templates_clear_dynamic();

// Register all built-in templates (v60, free_pour). Call once at startup.
void brew_templates_init();

#endif // HAS_SENSOR_HX711
