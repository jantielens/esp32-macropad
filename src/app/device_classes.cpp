// Device class aggregator.
//
// arduino-cli only compiles `.cpp` files in the sketch root. Each device-class
// implementation lives next to its feature code and is brought into the build
// by being `#include`d here under its HAS_* feature gate, the same pattern
// used by sensors.cpp, portal_components.cpp, widgets.cpp, etc.

#include "board_config.h"

#if HAS_EPAPER
#include "device_classes/epaper_device_class.cpp"
#endif

#if IS_SHUTTER_TESTER
#include "device_classes/shutter_tester_device_class.cpp"
#endif

// Called once from setup() before any device-class dispatch so each gated
// implementation registers itself with the runtime registry. Keeps
// registration explicit and ordered rather than relying on global ctors.
void device_classes_register_all() {
#if HAS_EPAPER
		epaper_device_class_register();
#endif
#if IS_SHUTTER_TESTER
                shutter_tester_device_class_register();
#endif
}
