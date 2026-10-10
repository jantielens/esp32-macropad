#include "board_config.h"
#include "board_hardware.h"

#ifdef BOARD_HARDWARE_SOURCE
#include BOARD_HARDWARE_SOURCE
#else
bool board_hardware_init() { return true; }

#if DISPLAY_DRIVER == DISPLAY_DRIVER_ARDUINO_GFX_CO5300
#include "drivers/arduino_gfx_co5300_driver.h"
CO5300PanelConfig board_co5300_config() { return CO5300PanelConfig{}; }
#endif

#if TOUCH_DRIVER == TOUCH_DRIVER_CST820B_WIRE
#include "drivers/wire_cst820b_touch_driver.h"
#include "i2c_bus.h"
#include <Wire.h>
static bool prepare_cst820b_bus() {
	static bool started = false;
	i2c_bus_init();
	if (!i2c_bus_lock()) return false;
	if (!started) started = Wire.begin(TOUCH_I2C_SDA, TOUCH_I2C_SCL);
	i2c_bus_unlock();
	return started;
}
CST820BConfig board_cst820b_config() {
	CST820BConfig config;
	config.prepare = prepare_cst820b_bus;
	return config;
}
#endif
#endif