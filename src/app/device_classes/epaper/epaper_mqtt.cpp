#include "epaper_mqtt.h"

#if HAS_EPAPER && HAS_MQTT

#include "epaper_battery.h"
#include "epaper_wake_budget.h"
#include "ha_discovery.h"
#include "log_manager.h"
#include "mqtt_manager.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_attr.h>

RTC_DATA_ATTR static bool g_epaper_discovery_published = false;

constexpr uint32_t kWakeDeliveryAckTimeoutMs = 250;
static char g_wake_topic[160] = {};
static uint64_t g_pending_wake_ack_session_id = 0;
static uint32_t g_pending_wake_ack_id = 0;
static bool g_pending_wake_ack_received = false;

bool epaper_mqtt_discovery_already_published() {
		return g_epaper_discovery_published;
}

void epaper_mqtt_mark_discovery_published() {
		g_epaper_discovery_published = true;
}

static const char* refresh_result_to_str(EpaperRefreshResult result) {
		switch (result) {
				case EpaperRefreshResult::Updated:     return "updated";
				case EpaperRefreshResult::Skipped:     return "skipped";
				case EpaperRefreshResult::FailedFetch: return "failed_fetch";
				case EpaperRefreshResult::FailedDraw:  return "failed_draw";
				case EpaperRefreshResult::Disabled:    return "disabled";
		}
		return "unknown";
}

static const char* wake_result_to_str(EpaperWakeResult result) {
		switch (result) {
				case EpaperWakeResult::InProgress:        return "in_progress";
				case EpaperWakeResult::Updated:           return "updated";
				case EpaperWakeResult::Skipped:           return "skipped";
				case EpaperWakeResult::FailedFetch:       return "failed_fetch";
				case EpaperWakeResult::FailedDraw:        return "failed_draw";
				case EpaperWakeResult::WifiFailed:        return "wifi_failed";
				case EpaperWakeResult::MqttConnectFailed: return "mqtt_connect_failed";
				case EpaperWakeResult::MqttPublishUnconfirmed: return "mqtt_publish_unconfirmed";
				case EpaperWakeResult::LowBattery:        return "low_battery";
				case EpaperWakeResult::ScheduleSuppressed:return "schedule_suppressed";
				case EpaperWakeResult::SourceUnconfigured:return "source_unconfigured";
				case EpaperWakeResult::BudgetExceeded:     return "budget_exceeded";
				case EpaperWakeResult::Interrupted:       return "interrupted";
		}
		return "unknown";
}

static const char* wake_reason_to_str(EpaperWakeReason reason) {
		switch (reason) {
				case EpaperWakeReason::Timer:    return "timer";
				case EpaperWakeReason::Button:   return "button";
				case EpaperWakeReason::ColdBoot: return "cold_boot";
		}
		return "unknown";
}

static const char* wake_stage_to_str(EpaperWakeStage stage) {
		switch (stage) {
				case EpaperWakeStage::Boot:        return "boot";
				case EpaperWakeStage::Battery:     return "battery";
				case EpaperWakeStage::Wifi:        return "wifi";
				case EpaperWakeStage::Refresh:     return "refresh";
				case EpaperWakeStage::Ntp:         return "ntp";
				case EpaperWakeStage::MqttConnect: return "mqtt_connect";
				case EpaperWakeStage::MqttPublish: return "mqtt_publish";
		}
		return "unknown";
}

static const char* budget_cut_to_str(uint8_t cut) {
		switch (static_cast<EpaperWakeBudgetCut>(cut)) {
				case EpaperWakeBudgetCut::None:    return "none";
				case EpaperWakeBudgetCut::Wifi:    return "wifi_budget";
				case EpaperWakeBudgetCut::Fetch:   return "fetch_budget";
				case EpaperWakeBudgetCut::Mqtt:    return "mqtt_budget";
				case EpaperWakeBudgetCut::Overall: return "overall_budget";
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
		doc["refresh_result"]  = refresh_result_to_str(outcome.result);
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

		if (timing) doc["wake_loop_ms"] = timing->total_active_ms;

		const bool ok = mqtt_manager.publishJson(topic, doc, true /*retained*/);
		if (ok) {
				LOGI("Epaper", "Published telemetry to %s", topic);
		} else {
				LOGW("Epaper", "MQTT telemetry publish failed");
		}
		return ok;
}

bool epaper_mqtt_prepare_wake_delivery() {
		extern MqttManager mqtt_manager;
		if (!mqtt_manager.connected()) return false;

		snprintf(g_wake_topic, sizeof(g_wake_topic), "%s/epaper/wake", mqtt_manager.baseTopic());
		g_pending_wake_ack_session_id = 0;
		g_pending_wake_ack_id = 0;
		g_pending_wake_ack_received = false;
		if (!mqtt_manager.subscribe(g_wake_topic)) {
			LOGW("Epaper", "Wake acknowledgement subscription failed");
			return false;
		}
		mqtt_manager.loop();
		return true;
}

void epaper_mqtt_on_message(const char* topic, const uint8_t* payload, unsigned int length) {
		if (!topic || !payload || g_pending_wake_ack_id == 0 ||
				strcmp(topic, g_wake_topic) != 0) return;

		StaticJsonDocument<128> doc;
		if (deserializeJson(doc, payload, length)) return;
		if (doc["session_id"].as<uint64_t>() == g_pending_wake_ack_session_id &&
				doc["wake_id"].as<uint32_t>() == g_pending_wake_ack_id) {
			g_pending_wake_ack_received = true;
		}
}

static bool publish_wake_record(const EpaperWakeRecord& record,
																bool deferred,
																uint32_t reporting_wake_id,
																uint32_t dropped_wake_records) {
		extern MqttManager mqtt_manager;
		if (!mqtt_manager.connected()) return false;

		char topic[160];
		snprintf(topic, sizeof(topic), "%s/epaper/wake", mqtt_manager.baseTopic());

		StaticJsonDocument<896> doc;
		doc["session_id"] = record.timing.session_id;
		doc["wake_id"] = record.timing.wake_id;
		doc["wake_reason"] = wake_reason_to_str(record.wake_reason);
		doc["result"] = wake_result_to_str(record.result);
		doc["last_stage"] = wake_stage_to_str(record.last_stage);
		doc["delivery"] = deferred ? "deferred" : "live";
		doc["reported_by_wake_id"] = reporting_wake_id;
		doc["battery_mv"] = record.battery_mv;
		doc["sidecar_http_status"] = record.sidecar_http_status;
		doc["crc_fetch_attempts"] = record.timing.crc_retry_count;
		doc["wifi_rssi"] = record.timing.wifi_rssi;
		doc["image_source"] = record.timing.image_from_cache ? "cache" : "download";
		if (record.refresh_result != record.result) {
			doc["refresh_result"] = wake_result_to_str(record.refresh_result);
		}
		if (record.previous_delivery.valid) {
			JsonObject previous_delivery = doc.createNestedObject("previous_delivery");
			previous_delivery["session_id"] = record.previous_delivery.session_id;
			previous_delivery["wake_id"] = record.previous_delivery.wake_id;
			previous_delivery["mqtt_connect_ms"] = record.previous_delivery.mqtt_connect_ms;
			previous_delivery["mqtt_publish_ms"] = record.previous_delivery.mqtt_publish_ms;
			previous_delivery["total_active_ms"] = record.previous_delivery.total_active_ms;
		}
		if (dropped_wake_records > 0) doc["dropped_wake_records"] = dropped_wake_records;

		JsonObject stages = doc.createNestedObject("stages_ms");
		stages["boot_to_wifi"] = record.timing.boot_to_wifi_ms;
		stages["refresh"] = record.timing.crc_to_draw_ms;
		stages["resolve"] = record.timing.resolve_ms;
		stages["fetch"] = record.timing.fetch_ms;
		stages["panel_draw"] = record.timing.draw_ms;
		stages["ntp"] = record.timing.ntp_sync_ms;
		stages["active_before_telemetry"] = record.timing.total_active_ms;
		JsonObject budget = doc.createNestedObject("budget");
		budget["overall_limit_ms"] = record.timing.overall_budget_ms;
		budget["elapsed_ms"] = record.timing.budget_elapsed_ms;
		budget["remaining_ms"] = record.timing.budget_remaining_ms;
		budget["wifi_limit_ms"] = record.timing.wifi_limit_ms;
		budget["fetch_limit_ms"] = record.timing.fetch_limit_ms;
		budget["mqtt_limit_ms"] = record.timing.mqtt_limit_ms;
		budget["wifi_target_ms"] = record.timing.wifi_target_ms;
		budget["fetch_target_ms"] = record.timing.fetch_target_ms;
		budget["mqtt_target_ms"] = record.timing.mqtt_target_ms;
		budget["cut"] = budget_cut_to_str(record.timing.budget_cut);

		return mqtt_manager.publishJson(topic, doc, false /*retained*/);
}

static bool publish_wake_record_confirmed(const EpaperWakeRecord& record,
																		 bool deferred, uint32_t reporting_wake_id,
																		 uint32_t dropped_wake_records) {
		extern MqttManager mqtt_manager;
		g_pending_wake_ack_session_id = record.timing.session_id;
		g_pending_wake_ack_id = record.timing.wake_id;
		g_pending_wake_ack_received = false;
		if (!publish_wake_record(record, deferred, reporting_wake_id, dropped_wake_records)) {
			g_pending_wake_ack_session_id = 0;
			g_pending_wake_ack_id = 0;
			return false;
		}

		const uint32_t start_ms = millis();
		while (!g_pending_wake_ack_received &&
				(millis() - start_ms) < kWakeDeliveryAckTimeoutMs) {
			mqtt_manager.loop();
			delay(10);
		}
		const bool confirmed = g_pending_wake_ack_received;
		g_pending_wake_ack_session_id = 0;
		g_pending_wake_ack_id = 0;
		if (!confirmed) LOGW("Epaper", "Wake %lu broker acknowledgement timed out",
				(unsigned long)record.timing.wake_id);
		return confirmed;
}

bool epaper_mqtt_publish_pending_wakes(uint32_t reporting_wake_id) {
		EpaperWakeRecord record = {};
		while (epaper_wake_journal_peek(&record)) {
			if (record.result == EpaperWakeResult::InProgress) return false;
			const bool deferred = record.timing.wake_id != reporting_wake_id;
			const uint32_t dropped_wake_records = epaper_wake_journal_dropped_count();
			if (!publish_wake_record_confirmed(record, deferred, reporting_wake_id, dropped_wake_records)) {
				if (!deferred) {
					epaper_wake_journal_mark_delivery_failed(EpaperWakeResult::MqttPublishUnconfirmed);
				}
				return false;
			}
			if (dropped_wake_records > 0) epaper_wake_journal_clear_dropped_count();
			epaper_wake_journal_remove_oldest();
		}
		return true;
}

bool epaper_mqtt_publish_ha_discovery(MqttManager& mqtt) {
	// Publishes retained HA discovery configs for the e-paper
		// telemetry surfaced under <base>/epaper/state. Entities are NOT marked
		// entity_category="diagnostic" so they appear together in the main
		// entity list of the device card; the "E-Paper" name prefix keeps them
		// visually grouped. WiFi RSSI is intentionally omitted -- the generic
		// wifi_rssi entity from ha_discovery.cpp already updates on every wake.
		char base[160];
		snprintf(base, sizeof(base), "%s/epaper/state", mqtt.baseTopic());

		const char* device_name = mqtt.friendlyName();
		const char* sanitized   = mqtt.sanitizedName();

		bool all_published = true;
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

				all_published &= mqtt.publishJson(cfg_topic, doc, true /*retained*/);
		};

		const char* legacy_timing_entities[] = {
				"epaper_boot_to_wifi_ms", "epaper_ntp_sync_ms", "epaper_crc_to_draw_ms",
				"epaper_draw_to_mqtt_ms", "epaper_last_elapsed_ms", "epaper_crc_retries",
				"epaper_resolve_ms", "epaper_fetch_ms", "epaper_draw_ms", "epaper_image_source",
		};
		for (const char* object_id : legacy_timing_entities) {
			char cfg_topic[192];
			snprintf(cfg_topic, sizeof(cfg_topic),
						 "homeassistant/sensor/%s/%s/config", sanitized, object_id);
			all_published &= mqtt.publish(cfg_topic, "", true /*retained*/);
		}

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

		publish_sensor("epaper_loop_ms", "E-Paper Wake Loop Time",
										 "{{ value_json.wake_loop_ms }}", "ms", "duration", "measurement");
		return all_published;
}

#endif // HAS_EPAPER && HAS_MQTT
