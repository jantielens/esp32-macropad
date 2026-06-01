// Darkroom Timer device-class — nav-only registration for the print session
// log page. The fragment JS talks to the /api/prints REST endpoint
// (web_portal_prints.cpp); no per-component config hooks are needed.
#include "board_config.h"

#if IS_DARKROOM_TIMER

#include "component_registry.h"
REGISTER_NAV_COMPONENT(prints, "prints", "darkroom", "Prints", 40, "prints")

#endif // IS_DARKROOM_TIMER
