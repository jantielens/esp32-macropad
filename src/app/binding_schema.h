#pragma once

// Emit the live binding scheme registry as a JSON array. `out_json` must be
// an ArduinoJson JsonArray pointer; this keeps the binding core independent of
// ArduinoJson for host-native tests.
void binding_schema_emit(void* out_json);