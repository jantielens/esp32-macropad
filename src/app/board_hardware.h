#pragma once

bool board_hardware_init();

class SensorRegistry;
void board_register_sensors(SensorRegistry& registry);