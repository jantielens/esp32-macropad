#pragma once

#include "board_config.h"
#if HAS_HA_LIVE
#include <ArduinoJson.h>

// Compact attribute suggestions for the existing dotted-object-path resolver.
const char* ha_lookup_describe_entity(ArduinoJson::JsonObjectConst state,
                                     const char* entity,
                                     ArduinoJson::JsonDocument& detail);
#endif
