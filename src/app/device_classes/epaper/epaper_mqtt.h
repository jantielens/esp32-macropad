#pragma once

#include "board_config.h"

#if HAS_EPAPER && HAS_MQTT

#include "epaper_refresh.h"
#include "epaper_timing.h"

class MqttManager;

// Publish a single retained JSON state burst to `<base>/epaper/state`
// summarising the last refresh attempt + timing budget. Best-effort;
// returns false if MQTT isn't connected or the payload won't fit.
bool epaper_mqtt_publish_state(const EpaperRefreshOutcome& outcome,
															 const EpaperTimingBudget* timing);

// Publish HA discovery entries for the e-paper telemetry sensors. Should
// only be called once per cold boot (caller-managed RTC flag).
void epaper_mqtt_publish_ha_discovery(MqttManager& mqtt);

// RTC-retained flag: true once HA discovery has been published this power
// cycle. Persists across deep sleep + soft resets; cleared only on cold
// boot (power loss). Used to skip the ~1-2s / ~2.5 KB discovery burst on
// every wake.
bool epaper_mqtt_discovery_already_published();
void epaper_mqtt_mark_discovery_published();

#endif // HAS_EPAPER && HAS_MQTT
