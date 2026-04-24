// portal_components.cpp — includes component definitions
//
// Arduino build system only compiles .cpp files in the sketch root directory.
// This translation unit centralizes the required component includes so each
// feature module can self-register via REGISTER_COMPONENT() without editing
// web_portal_routes.cpp. Same pattern as display_drivers.cpp.
//
// Components gated by compile flags are simply not included.

#include "board_config.h"

#if HAS_DISPLAY

#include "components/swipe_actions_component.cpp"
#include "components/boot_actions_component.cpp"
#include "components/button_defaults_component.cpp"
#include "components/timers_component.cpp"
#include "components/display_component.cpp"

#endif // HAS_DISPLAY
