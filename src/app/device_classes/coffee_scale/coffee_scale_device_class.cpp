// Coffee Scale device-class aggregator.
//
// arduino-cli only compiles `.cpp` files in the sketch root. All coffee-scale
// translation units live under `device_classes/coffee_scale/` and are pulled
// into the build by being `#include`d from this aggregator under the
// IS_COFFEE_SCALE gate, mirroring the shutter-tester aggregation pattern
// (see device_classes/shutter_tester_device_class.cpp).
//
// Phase 1 scaffold: this file is intentionally empty beyond the gated
// scaffold. Per-module `#include` lines and the DeviceClass struct land in
// subsequent phases as sensors, action types, brew engine, and portal
// surfaces are ported in.

#include "board_config.h"

#if IS_COFFEE_SCALE

// Phase 1 scaffold: empty registration so device_classes.cpp can call
// coffee_scale_device_class_register() symmetrically with the shutter and
// e-paper aggregators. The DeviceClass struct + device_class_register()
// call land in Phase 4 alongside the CoffeeScaleConfig singleton.
void coffee_scale_device_class_register() {
}

// Subsequent phases will add `#include` lines below for the per-module
// translation units (sensors, scale HAL, brew engine, web portal routes,
// components, etc.). Order does not matter beyond standard C++ TU rules
// because every coffee-scale .cpp gates its own contents on IS_COFFEE_SCALE
// or the derived HAS_SCALE / HAS_SENSOR_* flags.

#endif // IS_COFFEE_SCALE
