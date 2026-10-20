#pragma once

#include "board_config.h"
#if HAS_DISPLAY && (HAS_MQTT || HAS_HA_LIVE)
using BindingSourceVisitor = void (*)(const char* source, void* context);
// Visits cached pad sources with their named-binding context, no file I/O.
// Serializes with binding resolution using the display lock.
void binding_sources_visit(BindingSourceVisitor visitor, void* context);
#endif
