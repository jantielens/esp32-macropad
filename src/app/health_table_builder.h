#pragma once

#include <stddef.h>

typedef bool (*HealthTableLookupFn)(const char* key, char* out, size_t out_len);

// Build a structured health table payload for the Table widget.
// When extended is true, additional static device rows are included.
bool health_table_build(bool extended, HealthTableLookupFn lookup, char* out, size_t out_len);
