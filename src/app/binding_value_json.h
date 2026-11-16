#pragma once

#include <ArduinoJson.h>
#include <stddef.h>

// Convert a scalar without interpreting string contents as another template.
// Returns false for null, nonscalar values, or truncated output.
bool binding_value_scalar_json(JsonVariantConst value, char* out, size_t out_len);
