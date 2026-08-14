#pragma once
// ============================================================================
// Brew Template DSL — JSON parser / serializer for brew templates
// ============================================================================
// Host-testable: depends only on ArduinoJson + standard C++ (no ESP32 APIs).
// The loader (brew_template_loader.cpp) handles persistent-storage I/O and calls into
// these pure-parsing functions.
//
// JSON schema (v1):
//   { "v": 1, "name": "...", "display_name": "...", "description": "...",
//     "start_label": "...", "done_label": "...",
//     "idle_instruction": "...", "done_instruction": "...",
//     "stages": [ { "name": "...", "instruction": "...", "next_label": "...",
//                    "type": "manual|auto_weight|auto_time",
//                    "on_enter": ["tare","beep",...],
//                    "on_exit":  ["capture_dose",...],
//                    "auto_threshold": 2.0,
//                    "target_weight": 60.0,
//                    "target_flow_rate": 6.0,
//                    "target_time_s": 45,
//                    "capture": { "key": "...", "label": "...", "unit": "g" }
//                  }, ... ] }
// `target_time_s` is a progress target for any stage. Only `auto_time` stages
// advance automatically when their target is reached.

#include "brew_manager.h"   // BrewTemplate, BrewStage, BrewEffects, BrewStageType

// ---- Error codes returned by brew_dsl_parse() ----
#define BREW_DSL_OK                  0
#define BREW_DSL_ERR_JSON           -1   // ArduinoJson parse error
#define BREW_DSL_ERR_VERSION        -2   // unsupported "v" value
#define BREW_DSL_ERR_MISSING_NAME   -3   // "name" field missing or empty
#define BREW_DSL_ERR_NO_STAGES      -4   // "stages" missing or empty array
#define BREW_DSL_ERR_STAGE_TYPE     -5   // unknown stage type string
#define BREW_DSL_ERR_STAGE_NAME     -6   // stage missing "name"
#define BREW_DSL_ERR_ALLOC          -7   // heap allocation failed
#define BREW_DSL_ERR_TOO_MANY     -8   // more stages than BREW_DSL_MAX_STAGES
#define BREW_DSL_ERR_TARGET_TIME   -9   // target_time_s is not a valid whole-second duration

// Maximum number of stages per template
#define BREW_DSL_MAX_STAGES  16

// ---- Effect string ↔ bitmask ----

// Map a single effect name to its EFFECT_* flag. Returns EFFECT_NONE for
// unrecognised names (caller can choose to warn).
BrewEffects brew_dsl_parse_effect(const char* name);

// Return the canonical name for a single-bit effect flag, or nullptr.
const char* brew_dsl_effect_name(BrewEffects single_bit);

// ---- Stage type string ↔ enum ----

// Map a stage type name to its enum. Returns -1 if unrecognised.
int brew_dsl_parse_stage_type(const char* name);

// Return the canonical name for a stage type, or nullptr.
const char* brew_dsl_stage_type_name(BrewStageType t);

// ---- Full template parse / serialize ----

// Parse a JSON string into a heap-allocated BrewTemplate + BrewStage[].
// On success (*out_tmpl)->stages points to *out_stages, and is_dynamic=true.
// Caller owns the memory: delete[] *out_stages; delete *out_tmpl;
// On error, *out_tmpl and *out_stages are set to nullptr.
// err_buf (optional) receives a human-readable error message on failure.
int brew_dsl_parse(const char* json, size_t json_len,
                   BrewTemplate** out_tmpl, BrewStage** out_stages,
                   char* err_buf = nullptr, size_t err_buf_len = 0);

// Serialize a BrewTemplate (including its stages) to JSON.
// Returns the number of bytes written (excluding null terminator), or -1 on error.
int brew_dsl_serialize(const BrewTemplate* tmpl, char* buf, size_t buf_len);

// ---- Beep pattern duration calculator (host-testable) ----

// Calculate the total duration in milliseconds of a beep pattern DSL string.
// Pattern format: space-delimited tokens of "freq:dur" (tone) or "dur" (gap).
// Returns 0 for null/empty input.
uint32_t brew_dsl_beep_duration_ms(const char* pattern);
