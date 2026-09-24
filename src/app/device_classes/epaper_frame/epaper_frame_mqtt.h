#pragma once

#include "board_config.h"

#if IS_EPAPER_FRAME && HAS_MQTT

#include "epaper_frame_refresh.h"
#include "epaper_frame_timing.h"

class MqttManager;

// Publish a single retained JSON state burst to `<base>/epaper/state`
// summarising the last refresh attempt + timing budget. Best-effort;
// returns false if MQTT isn't connected or the payload won't fit.
bool epaper_frame_mqtt_publish_state(const EpaperRefreshOutcome& outcome,
															 const EpaperTimingBudget* timing);

// Compact RTC-retained summary for wakes that deliberately did not initialize
// WiFi/MQTT. Cleared only after the next retained state publish succeeds.
void epaper_frame_mqtt_record_offline_cycle(
		const EpaperRefreshOutcome& outcome, const char* image_key);

bool epaper_frame_mqtt_publish_pending_wakes(uint32_t reporting_wake_id);

// Subscribe to the wake event topic before publishing so the broker's echoed
// event confirms that a QoS 0 diagnostic record reached the broker.
bool epaper_frame_mqtt_prepare_wake_delivery();
void epaper_frame_mqtt_on_message(const char* topic, const uint8_t* payload, unsigned int length);

// Publish HA discovery entries for the e-paper telemetry sensors. Returns
// true only when every retained configuration message was accepted locally.
bool epaper_frame_mqtt_publish_ha_discovery(MqttManager& mqtt);

// RTC-retained flag: true once HA discovery has been published this power
// cycle. Persists across deep sleep + soft resets; cleared only on cold
// boot (power loss). Used to skip the ~1-2s / ~2.5 KB discovery burst on
// every wake.
bool epaper_frame_mqtt_discovery_already_published();
void epaper_frame_mqtt_mark_discovery_published();

#endif // IS_EPAPER_FRAME && HAS_MQTT
