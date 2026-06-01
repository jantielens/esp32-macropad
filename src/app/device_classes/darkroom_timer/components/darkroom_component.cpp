// Darkroom Timer device-class — nav-only registration for the relay
// configuration page. The fragment JS talks to the /api/relay REST endpoint
// (web_portal_relay.cpp); no per-component config hooks are needed.
#include "board_config.h"

#if IS_DARKROOM_TIMER

#include "component_registry.h"
REGISTER_NAV_COMPONENT(darkroom, "darkroom", "darkroom", "Configuration", 50, "darkroom")

#endif // IS_DARKROOM_TIMER
