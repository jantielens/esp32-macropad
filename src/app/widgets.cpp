// ============================================================================
// Widgets Compilation Unit
// ============================================================================
// Single compilation unit for all widget implementations.
// Arduino doesn't auto-compile .cpp files in subdirectories — this file
// ensures widget code is included in the build.

#include "board_config.h"

#if HAS_DISPLAY

#include "widgets/widget.cpp"
#include "widgets/bar_chart_widget.cpp"
#include "widgets/gauge_widget.cpp"
#include "widgets/sparkline_widget.cpp"

#endif // HAS_DISPLAY
