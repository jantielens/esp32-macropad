#pragma once

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Split entity/topic;path;format at the first two semicolons. Outputs are always
// terminated when buffers are valid. Returns false for invalid arguments or
// overflow; truncated outputs remain available for legacy MQTT consumers.
bool binding_value_parse_params(const char* params,
                                char* entity, size_t entity_len,
                                char* path, size_t path_len,
                                char* format, size_t format_len);

// Extract a dot-separated object path without truncating its keys.
// "." copies the raw payload. Invalid JSON copies the raw payload and returns
// false; a missing/null path clears out and returns false. Overflow returns false.
bool binding_value_extract_json(const char* payload, const char* path, char* out, size_t out_len);

// Apply one printf conversion to a value. Supports strings, integers, and finite
// floats with standard numeric length modifiers. Invalid/unsafe formats or
// unparseable values fall back to raw text. No dynamic widths or %n/%p/%c.
void binding_value_format(const char* raw_value, const char* format, char* out, size_t out_len);

bool binding_value_format_valid(const char* format, unsigned max_width, unsigned max_precision,
                                const char* conversions);

#ifdef __cplusplus
}

#include "board_config.h"
#if HAS_EXTERNAL_DATA
#include <ArduinoJson.h>

// Select a value in an already-parsed document, including array indices.
// Returns false for missing/null values, invalid paths or truncated output.
bool binding_value_select_json(JsonVariantConst root, const char* path,
                               char* out, size_t out_len);

#if HAS_MCP
// Inspect one bounded page of a cached JSON value using the same path rules.
bool binding_value_inspect_json(JsonVariantConst root, const char* path,
                                size_t offset, JsonObject result);
#endif

// Format external numeric values, including scientific notation.
void binding_value_format_external(const char* raw_value, const char* format, char* out, size_t out_len);
#endif
#endif
