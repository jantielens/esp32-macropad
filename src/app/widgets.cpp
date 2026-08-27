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
#include "widgets/bar_chart_widget.cpp"
#include "widgets/gauge_widget.cpp"
#include "widgets/sparkline_widget.cpp"
#include "widgets/table_widget.cpp"
#include "widgets/rocker_widget.cpp"
#include "widgets/numericrocker_widget.cpp"
#include "widgets/list_widget.cpp"
#if HAS_CAMERA
#include "widgets/camera_preview_widget.cpp"
#endif

#if HAS_NATIVE_EXTENSIONS
#include "widgets/external_widget.cpp"
#endif

#if IS_SHUTTER_TESTER
#include "device_classes/shutter_tester/widgets/waveform_widget.cpp"
#endif

#endif // HAS_DISPLAY
