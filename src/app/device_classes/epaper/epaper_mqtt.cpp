#include "epaper_mqtt.h"

#if HAS_EPAPER && HAS_MQTT

#include "epaper_battery.h"
#include "ha_discovery.h"
#include "log_manager.h"
#include "mqtt_manager.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_attr.h>

RTC_DATA_ATTR static bool g_epaper_discovery_published = false;

bool epaper_mqtt_discovery_already_published() {
		return g_epaper_discovery_published;
}

void epaper_mqtt_mark_discovery_published() {
		g_epaper_discovery_published = true;
}

static const char* result_to_str(EpaperRefreshResult r) {
		switch (r) {
				case EpaperRefreshResult::Updated:     return "updated";
				case EpaperRefreshResult::Skipped:     return "skipped";
				case EpaperRefreshResult::FailedFetch: return "failed_fetch";
				case EpaperRefreshResult::FailedDraw:  return "failed_draw";
				case EpaperRefreshResult::Disabled:    return "disabled";
		}
		return "unknown";
}

bool epaper_mqtt_publish_state(const EpaperRefreshOutcome& outcome,
													 const EpaperTimingBudget* timing) {
		extern MqttManager mqtt_manager;
		extern EpaperConfig g_epaper_config;
		extern uint8_t g_epaper_carousel_index;

		if (!mqtt_manager.connected()) return false;

		char topic[160];
		snprintf(topic, sizeof(topic), "%s/epaper/state", mqtt_manager.baseTopic());

		StaticJsonDocument<1024> doc;
		doc["battery_mv"]      = outcome.battery_mv;
		doc["battery_pct"]     = epaper_battery_percent(outcome.battery_mv);
		doc["wifi_rssi"]       = timing ? timing->wifi_rssi : (int16_t)WiFi.RSSI();
		doc["image_crc32"]     = outcome.crc_used;
		doc["refresh_result"]  = result_to_str(outcome.result);
		doc["refresh_count"]   = epaper_refresh_get_count();
		doc["sidecar_http_status"] = outcome.sidecar_http_status;
		doc["source_mode"] = epaper_source_uses_service(g_epaper_config.source_mode)
				? "service" : "slot-carousel";

		// Carousel telemetry
		doc["carousel_count"] = g_epaper_config.carousel_count;
		if (!epaper_source_uses_service(g_epaper_config.source_mode) &&
				g_epaper_config.carousel_count > 0) {
				doc["carousel_index"] = g_epaper_carousel_index;
				doc["carousel_url"] = g_epaper_config.carousel[g_epaper_carousel_index].url;
		}

		// Schedule telemetry
		doc["schedule_active"] = (g_epaper_config.schedule_hours != 0x00FFFFFF);
		doc["schedule_hours"] = g_epaper_config.schedule_hours;
		doc["schedule_tz_offset"] = g_epaper_config.schedule_tz_offset;

		JsonObject t = doc.createNestedObject("timing");
		if (timing) {
				t["boot_to_wifi_ms"]  = timing->boot_to_wifi_ms;
				t["ntp_sync_ms"]      = timing->ntp_sync_ms;
				t["crc_retry_count"]  = timing->crc_retry_count;
				t["crc_to_draw_ms"]   = timing->crc_to_draw_ms;
				t["draw_to_mqtt_ms"]  = timing->draw_to_mqtt_ms;
				t["total_active_ms"]  = timing->total_active_ms;
			t["resolve_ms"]       = timing->resolve_ms;
			t["fetch_ms"]         = timing->fetch_ms;
			t["draw_ms"]          = timing->draw_ms;
			t["image_source"]     = timing->image_from_cache ? "cache" : "download";
		}

		const bool ok = mqtt_manager.publishJson(topic, doc, true /*retained*/);
		if (ok) {
				LOGI("Epaper", "Published telemetry to %s", topic);
		} else {
				LOGW("Epaper", "MQTT telemetry publish failed");
		}
		return ok;
}

void epaper_mqtt_publish_ha_discovery(MqttManager& mqtt) {
	// Publishes seventeen retained HA discovery configs for the e-paper
		// telemetry surfaced under <base>/epaper/state. Entities are NOT marked
		// entity_category="diagnostic" so they appear together in the main
		// entity list of the device card; the "E-Paper" name prefix keeps them
		// visually grouped. WiFi RSSI is intentionally omitted -- the generic
		// wifi_rssi entity from ha_discovery.cpp already updates on every wake.
		char base[160];
		snprintf(base, sizeof(base), "%s/epaper/state", mqtt.baseTopic());

		const char* device_name = mqtt.friendlyName();
		const char* sanitized   = mqtt.sanitizedName();

		auto publish_sensor = [&](const char* object_id,
													const char* name_suffix,
													const char* value_template,
													const char* unit,
													const char* device_class,
													const char* state_class) {
				char cfg_topic[192];
				snprintf(cfg_topic, sizeof(cfg_topic),
								 "homeassistant/sensor/%s/%s/config", sanitized, object_id);

				StaticJsonDocument<512> doc;
				char unique_id[96];
				snprintf(unique_id, sizeof(unique_id), "%s_%s", sanitized, object_id);
				char name[96];
				snprintf(name, sizeof(name), "%s %s", device_name, name_suffix);

				doc["name"]   = name;
				doc["uniq_id"] = unique_id;
				doc["stat_t"] = base;
				doc["val_tpl"] = value_template;
				if (unit && *unit) doc["unit_of_meas"] = unit;
				if (device_class && *device_class) doc["dev_cla"] = device_class;
				if (state_class && *state_class) doc["stat_cla"] = state_class;

				JsonObject dev = doc.createNestedObject("dev");
				JsonArray ids = dev.createNestedArray("ids");
				ids.add(sanitized);
				dev["name"] = device_name;

				mqtt.publishJson(cfg_topic, doc, true /*retained*/);
		};

		// Core status entities.
		publish_sensor("epaper_battery", "Battery",
									 "{{ value_json.battery_pct }}", "%", "battery", "measurement");
		publish_sensor("epaper_battery_mv", "Battery Voltage",
									 "{{ value_json.battery_mv }}", "mV", "voltage", "measurement");
		delay(1);
		publish_sensor("epaper_refresh_count", "E-Paper Refresh Count",
									 "{{ value_json.refresh_count }}", "", "", "total_increasing");
		publish_sensor("epaper_last_result", "E-Paper Last Refresh Result",
									 "{{ value_json.refresh_result }}", "", "", "");
		delay(1);
		publish_sensor("epaper_image_crc", "E-Paper Image CRC",
									 "{{ '0x%08x' | format(value_json.image_crc32) }}", "", "", "");
		publish_sensor("epaper_sidecar_http", "E-Paper Sidecar HTTP Status",
									 "{{ value_json.sidecar_http_status }}", "", "", "measurement");
		delay(1);

		// Per-cycle timing budget. HA accepts "ms" as a duration unit since 2023.
		publish_sensor("epaper_loop_ms", "E-Paper Wake Loop Time",
									 "{{ value_json.timing.total_active_ms }}", "ms", "duration", "measurement");
		publish_sensor("epaper_boot_to_wifi_ms", "E-Paper Boot to WiFi",
									 "{{ value_json.timing.boot_to_wifi_ms }}", "ms", "duration", "measurement");
		publish_sensor("epaper_ntp_sync_ms", "E-Paper NTP Sync Time",
									 "{{ value_json.timing.ntp_sync_ms }}", "ms", "duration", "measurement");
		delay(1);
		publish_sensor("epaper_crc_to_draw_ms", "E-Paper CRC to Draw",
									 "{{ value_json.timing.crc_to_draw_ms }}", "ms", "duration", "measurement");
		publish_sensor("epaper_draw_to_mqtt_ms", "E-Paper Draw to MQTT",
									 "{{ value_json.timing.draw_to_mqtt_ms }}", "ms", "duration", "measurement");
		delay(1);
		publish_sensor("epaper_last_elapsed_ms", "E-Paper Refresh Elapsed",
									 "{{ value_json.timing.last_elapsed_ms }}", "ms", "duration", "measurement");
		publish_sensor("epaper_crc_retries", "E-Paper CRC Fetch Attempts",
									 "{{ value_json.timing.crc_retry_count }}", "", "", "measurement");
		delay(1);

	// Image-fetch/render breakdown (subset of crc_to_draw_ms): isolates the API
	// resolve, the SD-cache-or-download fetch, and the panel upload+refresh so
	// long-term trends (e.g. a slowing image API) are observable in HA.
	publish_sensor("epaper_resolve_ms", "E-Paper URL Resolve",
									 "{{ value_json.timing.resolve_ms }}", "ms", "duration", "measurement");
	publish_sensor("epaper_fetch_ms", "E-Paper Image Fetch",
									 "{{ value_json.timing.fetch_ms }}", "ms", "duration", "measurement");
	delay(1);
	publish_sensor("epaper_draw_ms", "E-Paper Panel Draw",
									 "{{ value_json.timing.draw_ms }}", "ms", "duration", "measurement");
	publish_sensor("epaper_image_source", "E-Paper Image Source",
									 "{{ value_json.timing.image_source }}", "", "", "");
	delay(1);
}

#endif // HAS_EPAPER && HAS_MQTT
