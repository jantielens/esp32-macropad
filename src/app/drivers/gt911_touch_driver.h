#ifndef GT911_TOUCH_DRIVER_H
#define GT911_TOUCH_DRIVER_H

#include "../touch_driver.h"
#include "../board_config.h"

#include <Wire.h>

// Minimal GT911 capacitive touch driver.
//
// The I2C bus is selected at compile time via TOUCH_I2C_BUS (board_config.h):
//   - Bus 1 (Wire1, default): Avoids ISR contention with WiFi on Core 0.
//     When Wire (bus 0) is initialized from Core 0, its ISR is pinned there.
//     LVGL polls touch from Core 1, so using bus 0 from Core 1 causes
//     interrupt watchdog timeouts when WiFi is active on Core 0.
//   - Bus 0 (Wire): Safe when WiFi runs on a separate co-processor (e.g.,
//     ESP32-P4 with external ESP32-C6 over SDIO — no I2C ISR contention).
//
// Optional hardware reset via TOUCH_RST pin:
//   - When TOUCH_RST >= 0, the driver toggles the reset pin during init.
//   - When TOUCH_INT >= 0, the INT pin state during reset selects the I2C
//     address (LOW → 0x5D, HIGH → 0x14).

// I2C bus abstraction: compile-time Wire/Wire1 selection
#if TOUCH_I2C_BUS == 0
#define GT911_WIRE  Wire
#define GT911_WIRE_NAME "Wire"
#else
#define GT911_WIRE  Wire1
#define GT911_WIRE_NAME "Wire1"
#endif

class GT911_TouchDriver : public TouchDriver {
public:
		GT911_TouchDriver();
		~GT911_TouchDriver() override = default;

		void init() override;

		bool isTouched() override;
		bool getTouch(uint16_t* x, uint16_t* y, uint16_t* pressure = nullptr) override;

		void setCalibration(uint16_t x_min, uint16_t x_max, uint16_t y_min, uint16_t y_max) override;
		void setRotation(uint8_t rotation) override;

private:
		uint8_t addr;
		uint8_t rotation;

		bool calibrationEnabled;
		uint16_t calXMin;
		uint16_t calXMax;
		uint16_t calYMin;
		uint16_t calYMax;

		// Cached state from last read()
		bool lastTouched;
		uint16_t lastX;
		uint16_t lastY;

		// Low-level GT911 I2C operations (Wire1)
		void gt911Read();
		void writeReg(uint16_t reg, uint8_t val);
		uint8_t readReg(uint16_t reg);
		void readBlock(uint16_t reg, uint8_t* buf, uint8_t len);

		void applyRotation(uint16_t& x, uint16_t& y) const;
};

#endif // GT911_TOUCH_DRIVER_H
