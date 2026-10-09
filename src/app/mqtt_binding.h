#pragma once

#include "board_config.h"

// Register the "mqtt" binding scheme. Values come from mqtt_sub_store, which
// only allocates when a broker is configured; the scheme registers regardless
// so pads and the binding reference know it on every MQTT display build.
void mqtt_binding_init();
