// Coffee Scale device-class — nav-only registration for the main Scale page.
#include "board_config.h"

#if IS_COFFEE_SCALE

#include "component_registry.h"
REGISTER_NAV_COMPONENT(coffee_scale, "scale", "coffee", "Scale", 10, "scale")

#endif // IS_COFFEE_SCALE
