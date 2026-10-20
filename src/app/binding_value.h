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
// false; a missing/null path clears out and returns false.
bool binding_value_extract_json(const char* payload, const char* path, char* out, size_t out_len);

// Apply one printf conversion to a value. Supports strings, integers, and finite
// floats with standard numeric length modifiers. Invalid/unsafe formats or
// unparseable values fall back to raw text. No dynamic widths or %n/%p/%c.
void binding_value_format(const char* raw_value, const char* format, char* out, size_t out_len);

#ifdef __cplusplus
}
#endif
