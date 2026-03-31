#pragma once

#include "board_config.h"

#if HAS_SCALE

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
// If a template with the same name already exists, it is replaced (the old
// dynamic entry is freed; a built-in pointer is simply overwritten).
void brew_template_register(const BrewTemplate* t);

// Look up a template by name. Falls back to "free_pour" when the requested
// name is not found (guarantees a non-null return after init).
const BrewTemplate* brew_template_find(const char* name);

// Iteration helpers for REST API / UI.
uint8_t brew_template_count();
const BrewTemplate* brew_template_get(uint8_t index);

// Remove all dynamically-allocated templates (called before reloading from FS).
void brew_templates_clear_dynamic();

// Register all built-in templates and load dynamic templates from LittleFS.
// Call once at startup.
void brew_templates_init();

#endif // HAS_SCALE
