// Coffee Scale device-class — nav-only registration for the Brew Templates page.
#include "board_config.h"

#if IS_COFFEE_SCALE

#include "component_registry.h"
REGISTER_NAV_COMPONENT(brew_templates, "brew-templates", "scale", "Brew Templates", 12, "brew-templates")

#endif // IS_COFFEE_SCALE
