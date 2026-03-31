#pragma once

#include "board_config.h"

#if HAS_SCALE

// Load all dynamic brew templates from /config/brew_templates/*.json on LittleFS.
// Each valid JSON file is parsed with brew_dsl_parse() and registered via
// brew_template_register(). Invalid files are logged and skipped.
// Called from brew_templates_init() after built-in registration.
void brew_template_loader_load();

// Clear all dynamic templates and reload from LittleFS.
// Called after REST API upload/delete operations.
void brew_template_loader_reload();

// LittleFS directory used for template storage.
#define BREW_TEMPLATE_DIR "/config/brew_templates"

#endif // HAS_SCALE
