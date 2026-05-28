#pragma once

#include "board_config.h"

#if IS_SHUTTER_TESTER

#include <ESPAsyncWebServer.h>

// GET  /api/shutter/tests       — read raw test script file content
// PUT  /api/shutter/tests       — write raw test script file content
// GET  /api/shutter/tests/list  — parsed test list (id, name, speed_count, shots_per_speed)

void web_portal_shutter_tests_register_routes(AsyncWebServer* server);

#endif // IS_SHUTTER_TESTER
