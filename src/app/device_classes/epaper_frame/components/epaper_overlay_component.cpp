// E-Paper Status Overlay portal component.
// Nav-only — overlay config (enabled / position / color / items bitmask) is
// saved through the shared /api/config endpoint, so this component just
// declares the nav entry + fragment.

#include "board_config.h"

#if IS_EPAPER_FRAME

#include "component_registry.h"

REGISTER_NAV_COMPONENT(epaper_frame_overlay,
    "epaper-overlay", "epaper_frame", "Status Overlay", 30, "epaper-overlay")

#endif // IS_EPAPER_FRAME
