#pragma once

// Emit the live binding scheme registry as a JSON array. `out_json` must be
// an ArduinoJson JsonArray pointer; this keeps the binding core independent of
// ArduinoJson for host-native tests. `include_docs` adds user-facing docs.
// When `scheme_filter` is set, emit only that scheme and return false if unknown.
bool binding_schema_emit(void* out_json, bool include_docs = false,
                         const char* scheme_filter = nullptr);