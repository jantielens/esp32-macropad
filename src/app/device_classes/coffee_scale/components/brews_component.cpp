// Coffee Scale device-class — nav-only registration for the Brews log page.
#include "board_config.h"

#if IS_COFFEE_SCALE

#include "component_registry.h"
REGISTER_NAV_COMPONENT(brews, "brews", "coffee", "Brews", 11, "brews")

#endif // IS_COFFEE_SCALE
