// ============================================================================
// Widgets Compilation Unit
// ============================================================================
// Single compilation unit for all widget implementations.
// Arduino doesn't auto-compile .cpp files in subdirectories — this file
// ensures widget code is included in the build.

#include <Arduino.h>
#include "board_config.h"

#if HAS_DISPLAY

#include "widgets/widget.cpp"
#include "widget_modules.h"

#endif // HAS_DISPLAY
