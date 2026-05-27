// E-Paper Image & Schedule portal component.
// Nav-only — config (image URL, rotation, wake interval, WiFi backoff,
// frontlight) is saved through the shared /api/config endpoint, so this
// component just declares the nav entry + fragment.

#include "board_config.h"

#if HAS_EPAPER

#include "component_registry.h"

REGISTER_NAV_COMPONENT(epaper_image,
    "epaper-image", "epaper", "Image & Schedule", 20, "epaper-image")

#endif // HAS_EPAPER
