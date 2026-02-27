#include "ble_advertiser.h"

#if HAS_BLE

#include "config_manager.h"
#include "power_config.h"
#include "project_branding.h"
#include "sensors/sensor_manager.h"

#include <NimBLEDevice.h>
#include <esp_sleep.h>
#include <math.h>

// NimBLE defines LOG_LEVEL_* macros that conflict with our logger enum.
#ifdef LOG_LEVEL_ERROR
#undef LOG_LEVEL_ERROR
#endif
#ifdef LOG_LEVEL_WARN
#undef LOG_LEVEL_WARN
#endif
#ifdef LOG_LEVEL_INFO
#undef LOG_LEVEL_INFO
#endif
#ifdef LOG_LEVEL_DEBUG
#undef LOG_LEVEL_DEBUG
#endif

#include "log_manager.h"

static bool g_ble_initialized = false;
static unsigned long g_last_ble_advertise = 0;

static uint16_t ms_to_adv_units(uint16_t ms) {
		// 0.625ms units
		uint32_t units = ((uint32_t)ms * 1000U) / 625U;
		if (units < 0x20) units = 0x20; // spec min
		if (units > 0x4000) units = 0x4000; // spec max
		return (uint16_t)units;
}

struct BTHomeField {
		uint8_t type;
		uint8_t data[8];
		uint8_t len;
		const char *label;
};

static bool append_field(uint8_t *buf, size_t buf_len, size_t *offset, const BTHomeField &field) {
		if (!buf || !offset) return false;
		const size_t needed = (size_t)field.len + 1; // type + data
		if (*offset + needed > buf_len) return false;

		buf[(*offset)++] = field.type;
		memcpy(buf + *offset, field.data, field.len);
		*offset += field.len;
		return true;
}

static bool try_add_u16(uint8_t type, uint16_t value, const char *label, BTHomeField &out) {
		out.type = type;
		out.data[0] = (uint8_t)(value & 0xFF);
		out.data[1] = (uint8_t)((value >> 8) & 0xFF);
		out.len = 2;
		out.label = label;
		return true;
}

static bool try_add_i16(uint8_t type, int16_t value, const char *label, BTHomeField &out) {
		out.type = type;
		out.data[0] = (uint8_t)(value & 0xFF);
		out.data[1] = (uint8_t)((value >> 8) & 0xFF);
		out.len = 2;
		out.label = label;
		return true;
}

static bool try_add_u8(uint8_t type, uint8_t value, const char *label, BTHomeField &out) {
		out.type = type;
		out.data[0] = value;
		out.len = 1;
		out.label = label;
		return true;
}

static bool build_bthome_fields(const JsonObject &sensors, BTHomeField *out_fields, size_t *out_count) {
		if (!out_fields || !out_count) return false;

		size_t count = 0;

		for (JsonPair kv : sensors) {
				const char *key = kv.key().c_str();
				if (!key || strlen(key) == 0) continue;

				JsonVariant value = kv.value();
				if (value.isNull()) continue;

				const bool is_bool = value.is<bool>();
				const bool is_number = value.is<int>() || value.is<float>() || value.is<double>();

				if (!is_bool && !is_number) continue;

				BTHomeField field = {};
				bool mapped = false;

				// Exact mappings
				if (strcmp(key, "temperature") == 0 || strcmp(key, "temp") == 0) {
						const float temp = value.as<float>();
						mapped = try_add_i16(0x02, (int16_t)roundf(temp * 100.0f), "temperature", field);
				} else if (strcmp(key, "dummy_value") == 0) {
						const float temp = value.as<float>();
						mapped = try_add_i16(0x02, (int16_t)roundf(temp * 100.0f), "dummy_temperature", field);
				} else if (strcmp(key, "humidity") == 0) {
						const float humidity = value.as<float>();
						mapped = try_add_u16(0x03, (uint16_t)roundf(humidity * 100.0f), "humidity", field);
				} else if (strcmp(key, "pressure") == 0) {
						const float pressure = value.as<float>();
						mapped = try_add_u16(0x04, (uint16_t)roundf(pressure * 100.0f), "pressure", field);
				} else if (strcmp(key, "presence") == 0) {
						const bool presence = value.as<bool>();
						mapped = try_add_u8(0x15, presence ? 1 : 0, "presence", field);
				} else if (strcmp(key, "battery") == 0) {
						const uint8_t battery = (uint8_t)constrain(value.as<int>(), 0, 100);
						mapped = try_add_u8(0x01, battery, "battery", field);
				}

				// Generic fallback based on key hints
				if (!mapped) {
						if (strstr(key, "temp") != nullptr && is_number) {
								const float temp = value.as<float>();
								mapped = try_add_i16(0x02, (int16_t)roundf(temp * 100.0f), "temperature", field);
						} else if (strstr(key, "hum") != nullptr && is_number) {
								const float humidity = value.as<float>();
								mapped = try_add_u16(0x03, (uint16_t)roundf(humidity * 100.0f), "humidity", field);
						} else if (strstr(key, "press") != nullptr && is_number) {
								const float pressure = value.as<float>();
								mapped = try_add_u16(0x04, (uint16_t)roundf(pressure * 100.0f), "pressure", field);
						} else if ((strstr(key, "presence") != nullptr || strstr(key, "motion") != nullptr) && is_bool) {
								const bool presence = value.as<bool>();
								mapped = try_add_u8(0x15, presence ? 1 : 0, "presence", field);
						}
				}

				// Vendor-specific fallback for unknown fields
				if (!mapped) {
						if (is_bool) {
								mapped = try_add_u8(0xF1, value.as<bool>() ? 1 : 0, key, field);
						} else if (is_number) {
								const float num = value.as<float>();
								mapped = try_add_i16(0xF0, (int16_t)roundf(num), key, field);
						}
				}

				if (!mapped) {
						LOGW("BLE", "No BTHome mapping for key '%s'", key);
						continue;
				}

				out_fields[count++] = field;
				if (count >= *out_count) break;
		}

		*out_count = count;
		return true;
}

bool ble_advertiser_init() {
		if (g_ble_initialized) return true;

		NimBLEDevice::init(PROJECT_DISPLAY_NAME);
		NimBLEDevice::setPower(ESP_PWR_LVL_P9);
		NimBLEDevice::setSecurityAuth(false, false, false);
		NimBLEDevice::setScanDuplicateCacheSize(0);

		g_ble_initialized = true;
		return true;
}

bool ble_advertiser_advertise_bthome(const DeviceConfig *config, const JsonObject &sensors, bool use_light_sleep) {
		if (!config) return false;

		if (!ble_advertiser_init()) {
				LOGE("BLE", "NimBLE init failed");
				return false;
		}

		BTHomeField fields[16];
		size_t field_count = sizeof(fields) / sizeof(fields[0]);
		build_bthome_fields(sensors, fields, &field_count);

		uint8_t payload[24];
		size_t payload_len = 0;

		// BTHome v2 unencrypted
		payload[payload_len++] = 0x40;

		for (size_t i = 0; i < field_count; i++) {
				if (!append_field(payload, sizeof(payload), &payload_len, fields[i])) {
						LOGW("BLE", "BTHome payload full; dropping '%s'", fields[i].label ? fields[i].label : "(unknown)");
						break;
				}
		}

		NimBLEAdvertisementData advData;
		NimBLEAdvertisementData scanResp;

		advData.setFlags(0x06);

		// Service data with UUID 0xFCD2
		advData.setServiceData(NimBLEUUID((uint16_t)0xFCD2), std::string((const char *)payload, payload_len));

		scanResp.setName(PROJECT_DISPLAY_NAME);

		NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
		adv->setAdvertisementData(advData);
		adv->setScanResponseData(scanResp);

		const uint16_t adv_interval_ms = config->ble_adv_interval_ms > 0 ? config->ble_adv_interval_ms : 100;
		const uint16_t interval_units = ms_to_adv_units(adv_interval_ms);
		adv->setMinInterval(interval_units);
		adv->setMaxInterval(interval_units);

		const uint16_t burst_ms = config->ble_adv_burst_ms > 0 ? config->ble_adv_burst_ms : 900;
		const uint16_t gap_ms = config->ble_adv_gap_ms > 0 ? config->ble_adv_gap_ms : 1100;
		const uint8_t bursts = config->ble_adv_bursts > 0 ? config->ble_adv_bursts : 2;

		LOGI("BLE", "Advertise BTHome (payload=%u bytes, bursts=%u, burst=%ums, gap=%ums, interval=%ums)",
				(unsigned)payload_len,
				(unsigned)bursts,
				(unsigned)burst_ms,
				(unsigned)gap_ms,
				(unsigned)adv_interval_ms
		);

		for (uint8_t i = 0; i < bursts; i++) {
				adv->start();
				delay(burst_ms);
				adv->stop();

				if (i + 1 < bursts) {
						if (use_light_sleep) {
								esp_sleep_enable_timer_wakeup((uint64_t)gap_ms * 1000ULL);
								esp_light_sleep_start();
						} else {
								delay(gap_ms);
						}
				}
		}

		return true;
}

void ble_advertiser_loop(const DeviceConfig *config, bool allow_advertise) {
		if (!config || !allow_advertise) return;

		const PublishTransport transport = power_config_parse_publish_transport(config);
		if (!power_config_transport_includes_ble(transport)) return;

		// Reuse duty-cycle interval for always-on BLE cadence to keep a single schedule knob.
		const uint32_t interval_seconds = config->cycle_interval_seconds;
		if (interval_seconds == 0) return;

		const unsigned long interval_ms = interval_seconds * 1000UL;
		const unsigned long now = millis();

		if (g_last_ble_advertise == 0 || (now - g_last_ble_advertise) >= interval_ms) {
				StaticJsonDocument<512> sensors_doc;
				JsonObject root = sensors_doc.to<JsonObject>();
				sensor_manager_append_mqtt(root);

				if (!ble_advertiser_advertise_bthome(config, root, false)) {
						LOGE("BLE", "Advertise failed");
				}

				g_last_ble_advertise = now;
		}
}

#endif // HAS_BLE
