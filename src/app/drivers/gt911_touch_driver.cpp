#include "gt911_touch_driver.h"

#include "../board_config.h"
#include "../log_manager.h"

// When touch shares Wire bus 0 with other peripherals (e.g. ES8311 audio
// codec), guard I2C transactions with the shared bus mutex.
#if TOUCH_I2C_BUS == 0
#include "../i2c_bus.h"
#define GT911_I2C_LOCK()   i2c_bus_lock()
#define GT911_I2C_UNLOCK() i2c_bus_unlock()
#else
#define GT911_I2C_LOCK()   ((void)0)
#define GT911_I2C_UNLOCK() ((void)0)
#endif

// GT911 register addresses
#define GT911_POINT_INFO  0x814E
#define GT911_POINT_1     0x814F

GT911_TouchDriver::GT911_TouchDriver()
		: addr(TOUCH_I2C_ADDR), rotation(0), calibrationEnabled(false),
			calXMin(0), calXMax(0), calYMin(0), calYMax(0) {}

void GT911_TouchDriver::init() {
		LOGI("GT911", "Initializing touch on %s (SDA=%d, SCL=%d, ADDR=0x%02X)",
				GT911_WIRE_NAME, TOUCH_I2C_SDA, TOUCH_I2C_SCL, TOUCH_I2C_ADDR);

		// ----------------------------------------------------------------
		// Optional hardware reset sequence.
		// When TOUCH_RST is connected, toggle it to ensure the GT911
		// starts in a known state.  If TOUCH_INT is also connected, its
		// level during the rising edge of RST selects the I2C address:
		//   INT=LOW  → 0x5D (TOUCH_I2C_ADDR default)
		//   INT=HIGH → 0x14 (TOUCH_I2C_ADDR_ALT)
		// ----------------------------------------------------------------
		#if TOUCH_RST >= 0
		LOGI("GT911", "Hardware reset (RST=GPIO%d)", TOUCH_RST);
		pinMode(TOUCH_RST, OUTPUT);

		#if defined(TOUCH_INT) && TOUCH_INT >= 0
		// Drive INT to select default I2C address before releasing reset.
		pinMode(TOUCH_INT, OUTPUT);
		digitalWrite(TOUCH_INT, (TOUCH_I2C_ADDR == 0x5D) ? LOW : HIGH);
		#endif

		// Reset pulse: LOW ≥10 ms, then HIGH + wait for boot.
		digitalWrite(TOUCH_RST, LOW);
		delay(10);
		digitalWrite(TOUCH_RST, HIGH);
		delay(50);  // GT911 boot time

		#if defined(TOUCH_INT) && TOUCH_INT >= 0
		// Release INT to allow normal interrupt operation.
		pinMode(TOUCH_INT, INPUT);
		#endif

		LOGI("GT911", "Reset complete");
		#endif

		// Initialize I2C bus.
		// Bus selection (Wire/Wire1) determined at compile time via GT911_WIRE.
		GT911_WIRE.begin(TOUCH_I2C_SDA, TOUCH_I2C_SCL, 400000);

		// Probe the controller to verify communication
		GT911_WIRE.beginTransmission(addr);
		uint8_t err = GT911_WIRE.endTransmission();
		if (err != 0) {
				// Try alternate address
				#ifdef TOUCH_I2C_ADDR_ALT
				LOGW("GT911", "Primary addr 0x%02X failed (err=%d), trying alt 0x%02X",
						addr, err, TOUCH_I2C_ADDR_ALT);
				addr = TOUCH_I2C_ADDR_ALT;
				GT911_WIRE.beginTransmission(addr);
				err = GT911_WIRE.endTransmission();
				if (err != 0) {
						LOGE("GT911", "Alt addr 0x%02X also failed (err=%d)", addr, err);
						return;
				}
				#else
				LOGE("GT911", "I2C probe failed (err=%d)", err);
				return;
				#endif
		}

		// Clear any pending touch data
		GT911_I2C_LOCK();
		const bool pending_cleared = writeReg(GT911_POINT_INFO, 0);
		GT911_I2C_UNLOCK();
		if (!pending_cleared) {
				LOGE("GT911", "Failed to clear pending touch data at addr 0x%02X", addr);
				return;
		}

	LOGI("GT911", "Touch initialized on %s (%dx%d, addr=0x%02X)",
			GT911_WIRE_NAME, DISPLAY_WIDTH, DISPLAY_HEIGHT, addr);
}

TouchReadStatus GT911_TouchDriver::gt911Read() {
		uint8_t touches = 0;
		uint16_t previous_ids = 0;
		uint16_t ids = 0;
		GT911_I2C_LOCK();
		const TouchReadStatus status = [&]() {
				uint8_t pointInfo = 0;
				if (!readBlock(GT911_POINT_INFO, &pointInfo, 1)) return TouchReadStatus::Error;
				if (!(pointInfo & 0x80)) return TouchReadStatus::Unchanged;
				touches = pointInfo & 0x0F;
				uint8_t data[8 * TOUCH_CONTACT_CAPACITY] = {};
				if (touches > TOUCH_CONTACT_CAPACITY || (touches && !readBlock(GT911_POINT_1, data, touches * 8)))
						return TouchReadStatus::Error;
				TouchSnapshot next;
				next.count = touches;
				for (uint8_t index = 0; index < lastSnapshot.count; ++index)
						previous_ids |= touch_contact_id_mask(lastSnapshot.contacts[index].id);
				for (uint8_t index = 0; index < touches; ++index) {
						const uint8_t* record = data + index * 8;
						const uint8_t id = record[0];
						const uint16_t mask = touch_contact_id_mask(id);
						if (!mask || (ids & mask)) return TouchReadStatus::Error;
						ids |= mask;
						next.contacts[index].id = id;
						next.contacts[index].horizontal = record[1] | (record[2] << 8);
						next.contacts[index].vertical = record[3] | (record[4] << 8);
				}
				if (!writeReg(GT911_POINT_INFO, 0)) return TouchReadStatus::Error;
				lastSnapshot = next;
				transformPending = true;
				return TouchReadStatus::Fresh;
		}();
		GT911_I2C_UNLOCK();
		if (status == TouchReadStatus::Fresh && ids != previous_ids)
				LOGI("GT911", "Contacts=%u IDs=0x%04x", unsigned(touches), unsigned(ids));
		return status;
}

bool GT911_TouchDriver::isTouched() {
		return gt911Read() != TouchReadStatus::Error && lastSnapshot.count;
}

bool GT911_TouchDriver::getTouch(uint16_t* x, uint16_t* y, uint16_t* pressure) {
		if (pressure) *pressure = 0;
		if (!x || !y) return false;

		const TouchSample sample = readSample();
		if (sample.status == TouchReadStatus::Error || !sample.pressed) return false;
		*x = sample.horizontal;
		*y = sample.vertical;
		return true;
}

TouchSample GT911_TouchDriver::readSample() {
		const TouchSnapshot snapshot = readSnapshot();
		TouchSample sample;
		sample.status = snapshot.status;
		sample.pressed = snapshot.count != 0;
		if (snapshot.count) {
				sample.horizontal = snapshot.contacts[0].horizontal;
				sample.vertical = snapshot.contacts[0].vertical;
		}
		return sample;
}

TouchSnapshot GT911_TouchDriver::readSnapshot() {
		const TouchReadStatus status = gt911Read();
		if (transformPending) {
				transformedSnapshot = lastSnapshot;
				for (uint8_t index = 0; index < transformedSnapshot.count; ++index)
						transform(transformedSnapshot.contacts[index].horizontal, transformedSnapshot.contacts[index].vertical);
				transformPending = false;
		}
		TouchSnapshot snapshot = transformedSnapshot;
		snapshot.status = status;
		return snapshot;
}

void GT911_TouchDriver::transform(uint16_t& tx, uint16_t& ty) const {

		// Apply calibration if configured
		if (calibrationEnabled && calXMax > calXMin && calYMax > calYMin) {
				uint32_t cx = tx;
				uint32_t cy = ty;
				if (cx < calXMin) cx = calXMin;
				if (cx > calXMax) cx = calXMax;
				if (cy < calYMin) cy = calYMin;
				if (cy > calYMax) cy = calYMax;

				tx = (uint16_t)((cx - calXMin) * (uint32_t)(DISPLAY_WIDTH - 1) / (uint32_t)(calXMax - calXMin));
				ty = (uint16_t)((cy - calYMin) * (uint32_t)(DISPLAY_HEIGHT - 1) / (uint32_t)(calYMax - calYMin));
		}

		applyRotation(tx, ty);
}

void GT911_TouchDriver::setCalibration(uint16_t x_min, uint16_t x_max, uint16_t y_min, uint16_t y_max) {
		calibrationEnabled = true;
		calXMin = x_min;
		calXMax = x_max;
		calYMin = y_min;
		calYMax = y_max;
		transformPending = true;
}

void GT911_TouchDriver::setRotation(uint8_t r) {
		rotation = r & 0x03;
		transformPending = true;
}

// ============================================================================
// Low-level I2C (GT911_WIRE — compile-time bus selection)
// ============================================================================

bool GT911_TouchDriver::writeReg(uint16_t reg, uint8_t val) {
		GT911_WIRE.beginTransmission(addr);
		GT911_WIRE.write(highByte(reg));
		GT911_WIRE.write(lowByte(reg));
		GT911_WIRE.write(val);
		return GT911_WIRE.endTransmission() == 0;
}

bool GT911_TouchDriver::readBlock(uint16_t reg, uint8_t* buf, uint8_t len) {
		GT911_WIRE.beginTransmission(addr);
		GT911_WIRE.write(highByte(reg));
		GT911_WIRE.write(lowByte(reg));
		if (GT911_WIRE.endTransmission() != 0 || GT911_WIRE.requestFrom(addr, len) != len) return false;
		for (uint8_t i = 0; i < len; i++) {
				const int value = GT911_WIRE.read();
				if (value < 0) return false;
				buf[i] = uint8_t(value);
		}
		return true;
}

void GT911_TouchDriver::applyRotation(uint16_t& x, uint16_t& y) const {
		uint16_t w = DISPLAY_WIDTH;
		uint16_t h = DISPLAY_HEIGHT;

		switch (rotation) {
				case 0:
						return;
				case 1: {
						uint16_t nx = y;
						uint16_t ny = (uint16_t)(w - 1 - x);
						x = nx;
						y = ny;
						return;
				}
				case 2:
						x = (uint16_t)(w - 1 - x);
						y = (uint16_t)(h - 1 - y);
						return;
				case 3: {
						uint16_t nx = (uint16_t)(h - 1 - y);
						uint16_t ny = x;
						x = nx;
						y = ny;
						return;
				}
				default:
						return;
		}
}
