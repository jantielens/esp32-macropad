#pragma once

#include <sdkconfig.h>

// Applied to every source unit so NimBLE-Arduino has one consistent profile.
// Remove SDK compatibility aliases that would re-enable disabled host roles.
#undef CONFIG_NIMBLE_ROLE_CENTRAL
#undef CONFIG_NIMBLE_ROLE_OBSERVER
#undef CONFIG_BT_NIMBLE_ROLE_CENTRAL
#define CONFIG_BT_NIMBLE_ROLE_CENTRAL 0
#undef CONFIG_BT_NIMBLE_ROLE_OBSERVER
#define CONFIG_BT_NIMBLE_ROLE_OBSERVER 0
// This library's GAP/GATT initialization still needs peripheral-host symbols.
// No server is created, and every advertisement is explicitly nonconnectable.
#undef CONFIG_BT_NIMBLE_ENABLE_CONN_REATTEMPT
#define CONFIG_BT_NIMBLE_ENABLE_CONN_REATTEMPT 0
// The ESP32-S3 controller requires a nonzero connection capacity.
#undef CONFIG_BT_NIMBLE_MAX_CONNECTIONS
#define CONFIG_BT_NIMBLE_MAX_CONNECTIONS 1
#undef CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_INTERNAL
#define CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL 1
