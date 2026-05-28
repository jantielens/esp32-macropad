#include "class_branding.h"
#include "board_config.h"

// Detection precedence (top to bottom):
//   1. IS_* product variant flags (none defined yet; pattern reserved for
//      future ports of darkroom-timer, shutter-tester, coffee-scale, etc.)
//   2. HAS_EPAPER  -> "E-Paper"
//   3. !HAS_DISPLAY -> "Headless"
//   4. default     -> "Macropad" (interactive display boards)

const char* device_class_get_display_name() {
#if HAS_EPAPER
		return "E-Paper";
#elif !HAS_DISPLAY
		return "Headless";
#else
		return "Macropad";
#endif
}

const char* device_class_get_slug() {
#if HAS_EPAPER
		return "EPAPER";
#elif !HAS_DISPLAY
		return "HEADLESS";
#else
		return "MACROPAD";
#endif
}

const char* device_class_get_full_name() {
#if HAS_EPAPER
		return "ESP32-MP E-Paper";
#elif !HAS_DISPLAY
		return "ESP32-MP Headless";
#else
		return "ESP32 Macropad";
#endif
}
