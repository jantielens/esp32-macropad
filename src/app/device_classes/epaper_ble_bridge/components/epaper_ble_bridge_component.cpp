#include "board_config.h"

#if IS_EPAPER_BLE_BRIDGE

#include "component_registry.h"

REGISTER_NAV_COMPONENT(epaper_ble_bridge, "epaper-ble-bridge", "bridge",
                       "Frame Assignments", 10, "epaper-ble-bridge")

#endif