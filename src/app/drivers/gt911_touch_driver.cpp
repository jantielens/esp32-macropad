#include "gt911_touch_driver.h"

#include "../board_config.h"
#include "../log_manager.h"
#include "gt911_config.h"
#include <stdio.h>
#include <string.h>
#if HAS_USB_HID
#include "../mouse_hid.h"
#endif

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
			calXMin(0), calXMax(0), calYMin(0), calYMax(0),
			lastTouched(false), lastX(0), lastY(0) {}

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
		writeReg(GT911_POINT_INFO, 0);
		logDiagnostics();

	#if TOUCH_GT911_FILTER >= 0
	configureFilter();
	#endif

	LOGI("GT911", "Touch initialized on %s (%dx%d, addr=0x%02X)",
			GT911_WIRE_NAME, DISPLAY_WIDTH, DISPLAY_HEIGHT, addr);
}

void GT911_TouchDriver::logDiagnostics() {
		uint8_t identity[11]{};
		uint8_t config[0x8140 - gt911_config::start_register]{};
		uint8_t verified[sizeof(config)]{};
		constexpr size_t core_size = gt911_config::size + 1;
		auto read_config = [&](uint8_t* buffer) {
				for (size_t offset = 0; offset < core_size; offset += 16) {
						const size_t remaining = core_size - offset;
						const uint8_t count = remaining < 16 ? remaining : 16;
						if (!readBlock(gt911_config::start_register + offset, buffer + offset, count)) return false;
				}
				return true;
		};
		GT911_I2C_LOCK();
		traceIoErrors = 0;
		const bool identity_ok = readBlock(0x8140, identity, sizeof(identity));
		const bool config_ok = read_config(config);
		const bool stable = config_ok && read_config(verified) && memcmp(config, verified, core_size) == 0;
		const bool extended_ok = config_ok && readBlock(0x8101, config + core_size, sizeof(config) - core_size);
		const bool extended_stable = extended_ok && readBlock(0x8101, verified + core_size, sizeof(config) - core_size) &&
				memcmp(config + core_size, verified + core_size, sizeof(config) - core_size) == 0;
		const uint8_t errors = traceIoErrors;
		GT911_I2C_UNLOCK();
		LOGI("GT911", "Diagnostics before config writes: addr=0x%02X filter-target=%d version-reset=%d errors=0x%02X",
				addr, TOUCH_GT911_FILTER, TOUCH_GT911_RESET_CONFIG_VERSION, errors);
		if (identity_ok) {
				char product[5]{};
				for (size_t index = 0; index < 4; ++index) {
						product[index] = identity[index] >= 32 && identity[index] <= 126 ? identity[index] : '.';
				}
				LOGI("GT911", "identity[8140..814A]=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
						identity[0], identity[1], identity[2], identity[3], identity[4], identity[5],
						identity[6], identity[7], identity[8], identity[9], identity[10]);
				LOGI("GT911", "product='%s' firmware=0x%04X output=%ux%u sensor/vendor=0x%02X",
						product, identity[4] | (identity[5] << 8), identity[6] | (identity[7] << 8),
						identity[8] | (identity[9] << 8), identity[10]);
		} else LOGW("GT911", "Identity read failed; controller model remains unverified");
		if (!config_ok) {
				LOGW("GT911", "Configuration snapshot read failed");
				return;
		}
		LOGI("GT911", "config[8047..8100]: bytes=%u stable=%d (GT911 layout; check product ID)",
				static_cast<unsigned>(core_size), stable);
		LOGI("GT911", "extended[8101..813F]: valid=%d stable=%d (raw; product-specific)", extended_ok, extended_stable);
		const size_t snapshot_size = extended_ok ? sizeof(config) : core_size;
		for (size_t offset = 0; offset < snapshot_size; offset += 16) {
				char hex[16 * 3 + 1]{};
				const size_t remaining = snapshot_size - offset;
				const size_t count = remaining < 16 ? remaining : 16;
				for (size_t index = 0; index < count; ++index) {
						snprintf(hex + index * 3, sizeof(hex) - index * 3, "%02X ", config[offset + index]);
				}
				LOGI("GT911", "config[0x%04X]=%s", static_cast<unsigned>(gt911_config::start_register + offset), hex);
		}
		LOGI("GT911", "GT911-layout: version=0x%02X output=%ux%u contacts=%u",
				config[0], config[1] | (config[2] << 8), config[3] | (config[4] << 8), config[5] & 0x0F);
		LOGI("GT911", "GT911-layout: checksum[80FF]=0x%02X computed=0x%02X fresh[8100]=0x%02X",
				config[gt911_config::data_size], gt911_config::checksum(config), config[gt911_config::size]);
		LOGI("GT911", "GT911-layout: filter[8050]=0x%02X first=%u normal=%u debounce[804F]=0x%02X",
				config[9], config[9] >> 6, config[9] & 0x3F, config[8]);
		LOGI("GT911", "GT911-layout: refresh[8056]=0x%02X nominal-period=%ums low-power[8055]=0x%02X",
				config[15], 5 + (config[15] & 0x0F), config[14]);
		LOGI("GT911", "GT911-layout: switch1[804D]=0x%02X stretch-rank=%u noise[8052]=0x%02X touch/leave=%u/%u",
				config[6], (config[6] >> 4) & 3, config[11], config[12], config[13]);
}

#if TOUCH_GT911_FILTER >= 0
void GT911_TouchDriver::configureFilter() {
		static_assert(TOUCH_GT911_FILTER <= 255, "GT911 filter must fit in one byte");
		uint8_t config[gt911_config::size + 1];
		uint8_t verified[gt911_config::size];
		auto read_config = [&](uint8_t* buffer) {
				for (size_t offset = 0; offset < gt911_config::size; offset += 16) {
						const size_t remaining = gt911_config::size - offset;
						const uint8_t count = remaining < 16 ? remaining : 16;
						const uint16_t reg = gt911_config::start_register + offset;
						GT911_WIRE.beginTransmission(addr);
						GT911_WIRE.write(highByte(reg));
						GT911_WIRE.write(lowByte(reg));
						if (GT911_WIRE.endTransmission() != 0 || GT911_WIRE.requestFrom(addr, count) != count) return false;
						for (uint8_t index = 0; index < count; ++index) buffer[offset + index] = GT911_WIRE.read();
				}
				return true;
		};

		GT911_I2C_LOCK();
		const bool read_ok = read_config(config) && read_config(verified);
		GT911_I2C_UNLOCK();
		if (!read_ok) {
				LOGW("GT911", "Filter override skipped: configuration read failed");
				return;
		}
		for (size_t index = 0; index < gt911_config::size; ++index) {
				if (verified[index] != config[index]) {
						LOGW("GT911", "Filter override skipped: configuration reads differ at 0x%04X",
								static_cast<unsigned>(gt911_config::start_register + index));
						return;
				}
		}
		const uint16_t max_x = config[1] | (config[2] << 8);
		const uint16_t max_y = config[3] | (config[4] << 8);
		const uint8_t contacts = config[5] & 0x0F;
		if (!max_x || max_x > 4096 || !max_y || max_y > 4096 || !contacts || contacts > 5) {
				LOGW("GT911", "Filter override skipped: implausible configuration (%ux%u, contacts=%u)",
						max_x, max_y, contacts);
				return;
		}
		const uint8_t old_filter = config[gt911_config::filter_offset];
		if (old_filter == TOUCH_GT911_FILTER) {
				LOGI("GT911", "Filter override already active: 0x%02X", old_filter);
				return;
		}
		const uint8_t old_version = config[0];
		constexpr bool reset_version = TOUCH_GT911_RESET_CONFIG_VERSION;
		if (old_version >= 90 && !reset_version) {
				LOGW("GT911", "Filter override skipped: vendor fixed configuration version=0x%02X (%u); filter remains 0x%02X",
						config[0], config[0], old_filter);
				return;
		}
		const uint8_t expected_version = reset_version ? gt911_config::initial_version : old_version;
		if (reset_version) {
				LOGW("GT911", "Experimental version reset: 0x%02X -> write 0x00; expected readback=0x%02X; configuration may persist",
						old_version, expected_version);
		}
		const uint8_t computed_checksum = gt911_config::checksum(config);
		LOGI("GT911", "Stable configuration: %ux%u contacts=%u checksum read=0x%02X computed=0x%02X; regenerating for write",
				max_x, max_y, contacts, config[gt911_config::data_size], computed_checksum);
		gt911_config::set_filter(config, TOUCH_GT911_FILTER, reset_version);
		config[gt911_config::size] = 1;

		GT911_I2C_LOCK();
		const size_t required_buffer = sizeof(config) + 2;
		if (GT911_WIRE.setBufferSize(required_buffer) < required_buffer) {
				GT911_I2C_UNLOCK();
				LOGW("GT911", "Filter override skipped: cannot allocate contiguous I2C write buffer");
				return;
		}
		GT911_WIRE.beginTransmission(addr);
		GT911_WIRE.write(highByte(gt911_config::start_register));
		GT911_WIRE.write(lowByte(gt911_config::start_register));
		const bool queued = GT911_WIRE.write(config, sizeof(config)) == sizeof(config);
		const uint8_t error = GT911_WIRE.endTransmission();
		GT911_I2C_UNLOCK();
		if (!queued || error != 0) {
				LOGW("GT911", "Filter override contiguous write failed (queued=%d I2C=%u); controller state unverified", queued, error);
				return;
		}
		LOGI("GT911", "Filter configuration sent: %u bytes in one I2C transaction (version=0x%02X)",
				static_cast<unsigned>(sizeof(config)), config[0]);
		delay(20);
		GT911_I2C_LOCK();
		const bool verify_ok = read_config(verified);
		GT911_I2C_UNLOCK();
		if (!verify_ok) {
				LOGW("GT911", "Filter override verification read failed");
				return;
		}
		for (size_t index = 0; index < gt911_config::data_size; ++index) {
				const uint8_t expected = index == 0 ? expected_version : config[index];
				if (verified[index] != expected) {
						LOGW("GT911", "Filter override verification mismatch at 0x%04X (expected 0x%02X, read 0x%02X)",
								static_cast<unsigned>(gt911_config::start_register + index), expected, verified[index]);
						return;
				}
		}
		LOGI("GT911", "Filter override applied: 0x%02X -> 0x%02X; version 0x%02X -> 0x%02X; remaining configuration preserved",
				old_filter, TOUCH_GT911_FILTER, old_version, verified[0]);
}
#endif

void GT911_TouchDriver::gt911Read() {
		#if HAS_USB_HID
		const bool trace_ack = mouse_hid_trace_touch_is_active();
		const uint32_t trace_start_us = micros();
		#endif
		GT911_I2C_LOCK();
		traceIoErrors = 0;
		#if HAS_USB_HID
		const uint32_t trace_locked_us = micros();
		if (!trace_ack) traceAckSchedule = {};
		uint8_t trace_id = 0;
		uint16_t trace_size = 0;
		uint8_t trace_report[8]{};
		bool trace_report_valid = false;
		uint8_t post_ack_status = 0;
		uint8_t post_ack_errors = 0;
		uint32_t ack_probe_us = 0;
		bool post_ack_checked = false;
		#endif
		uint8_t pointInfo = readReg(GT911_POINT_INFO);
		uint8_t bufferStatus = (pointInfo >> 7) & 1;
		uint8_t touches = pointInfo & 0x0F;
		#if HAS_USB_HID
		auto capture_trace = [&]() {
				MouseTraceTouchSample sample{millis(), trace_locked_us - trace_start_us, micros() - trace_locked_us,
						lastX, lastY, trace_size, trace_id, pointInfo, traceIoErrors};
				memcpy(sample.report, trace_report, sizeof(sample.report));
				sample.report_valid = trace_report_valid;
				sample.post_ack_status = post_ack_status;
				sample.post_ack_errors = post_ack_errors;
				sample.ack_probe_us = ack_probe_us;
				sample.post_ack_checked = post_ack_checked;
				return sample;
		};
		#endif

		// Only update state when the GT911 has completed a new scan.
		// When bufferStatus==0, no new data is available — keep the previous
		// touch state to avoid inserting a false RELEASED between scans.
		// The GT911 scans at ~60-140 Hz; the LVGL task can poll much faster,
		// so empty reads are expected while the finger is still down.
		if (bufferStatus == 0) {
				#if HAS_USB_HID
				const MouseTraceTouchSample trace = capture_trace();
				#endif
				GT911_I2C_UNLOCK();
				#if HAS_USB_HID
				mouse_hid_trace_touch(trace);
				#endif
				return;
		}

		lastTouched = (touches > 0);

		if (lastTouched) {
				uint8_t data[8]{};
				const bool report_valid = readBlock(GT911_POINT_1, data, sizeof(data));
				lastX = data[1] | (data[2] << 8);
				lastY = data[3] | (data[4] << 8);
				#if HAS_USB_HID
				trace_id = data[0] & 0x0F;
				trace_size = data[5] | (data[6] << 8);
				memcpy(trace_report, data, sizeof(trace_report));
				trace_report_valid = report_valid;
				#else
				(void)report_valid;
				#endif
		}

		// Clear buffer status flag (must always be done after reading)
		writeReg(GT911_POINT_INFO, 0);
		#if HAS_USB_HID
		const uint32_t ack_written_us = micros();
		if (trace_ack) {
				const uint8_t report_errors = traceIoErrors;
				traceIoErrors = 0;
				const uint32_t probe_start_us = micros();
				post_ack_status = readReg(GT911_POINT_INFO);
				ack_probe_us = micros() - probe_start_us;
				post_ack_errors = traceIoErrors;
				post_ack_checked = true;
				traceIoErrors = report_errors;
		}
		MouseTraceTouchSample trace = capture_trace();
		if (trace_ack && !trace.errors && trace.report_valid) {
				const uint16_t target_us = traceAckSchedule.take_delay_us(ack_written_us, true);
				if (target_us) {
						const uint32_t elapsed_us = micros() - ack_written_us;
						if (elapsed_us < target_us) delayMicroseconds(target_us - elapsed_us);
						MouseTraceDelayedAck& delayed = trace.delayed_ack;
						delayed.target_us = target_us;
						traceIoErrors = 0;
						const uint32_t probe_start_us = micros();
						delayed.status = readReg(GT911_POINT_INFO);
						delayed.elapsed_us = micros() - ack_written_us;
						delayed.status_errors = traceIoErrors;
						traceIoErrors = 0;
						delayed.report_valid = readBlock(GT911_POINT_1, delayed.report, sizeof(delayed.report));
						delayed.report_errors = traceIoErrors;
						delayed.read_us = micros() - probe_start_us;
						traceIoErrors = trace.errors;
				}
		}
		#endif
		GT911_I2C_UNLOCK();
		#if HAS_USB_HID
		mouse_hid_trace_touch(trace);
		#endif
}

bool GT911_TouchDriver::isTouched() {
		// Perform a fresh I2C read so callers outside the LVGL indev callback
		// (e.g., screen-saver wake poll) get current hardware state.
		gt911Read();
		return lastTouched;
}

bool GT911_TouchDriver::getTouch(uint16_t* x, uint16_t* y, uint16_t* pressure) {
		if (pressure) *pressure = 0;
		if (!x || !y) return false;

		gt911Read();
		if (!lastTouched) return false;

		uint16_t tx = lastX;
		uint16_t ty = lastY;

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

		*x = tx;
		*y = ty;
		return true;
}

void GT911_TouchDriver::setCalibration(uint16_t x_min, uint16_t x_max, uint16_t y_min, uint16_t y_max) {
		calibrationEnabled = true;
		calXMin = x_min;
		calXMax = x_max;
		calYMin = y_min;
		calYMax = y_max;
}

void GT911_TouchDriver::setRotation(uint8_t r) {
		rotation = r & 0x03;
}

// ============================================================================
// Low-level I2C (GT911_WIRE — compile-time bus selection)
// ============================================================================

void GT911_TouchDriver::writeReg(uint16_t reg, uint8_t val) {
		GT911_WIRE.beginTransmission(addr);
		GT911_WIRE.write(highByte(reg));
		GT911_WIRE.write(lowByte(reg));
		GT911_WIRE.write(val);
		if (GT911_WIRE.endTransmission() != 0) traceIoErrors |= 1;
}

uint8_t GT911_TouchDriver::readReg(uint16_t reg) {
		GT911_WIRE.beginTransmission(addr);
		GT911_WIRE.write(highByte(reg));
		GT911_WIRE.write(lowByte(reg));
		if (GT911_WIRE.endTransmission() != 0) traceIoErrors |= 1;
		if (GT911_WIRE.requestFrom(addr, (uint8_t)1) != 1) traceIoErrors |= 2;
		return GT911_WIRE.read();
}

bool GT911_TouchDriver::readBlock(uint16_t reg, uint8_t* buf, uint8_t len) {
		GT911_WIRE.beginTransmission(addr);
		GT911_WIRE.write(highByte(reg));
		GT911_WIRE.write(lowByte(reg));
		const uint8_t error = GT911_WIRE.endTransmission();
		const uint8_t received = GT911_WIRE.requestFrom(addr, len);
		if (error != 0) traceIoErrors |= 1;
		if (received != len) traceIoErrors |= 2;
		for (uint8_t i = 0; i < len; i++) {
				buf[i] = GT911_WIRE.read();
		}
		return error == 0 && received == len;
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
