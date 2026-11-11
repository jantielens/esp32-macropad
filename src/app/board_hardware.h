#pragma once

#include "board_config.h"
#ifdef BOARD_HARDWARE_HEADER
#include BOARD_HARDWARE_HEADER
#endif

bool board_hardware_init();

class SensorRegistry;
void board_register_sensors(SensorRegistry& registry);