#include "mqtt_screen.h"

#include "board_config.h"

#if HAS_MQTT && HAS_DISPLAY

#include "mqtt_manager.h"
#include "display_manager.h"
#include "screen_saver_manager.h"
#include "log_manager.h"

#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <string.h>

static const char* TAG = "MqttScreen";

static char g_command_topic[128] = {0};
static char g_state_topic[128] = {0};

// Pending command from MQTT callback (cross-task)
static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
static char g_pending_screen[16] = {0};
static volatile bool g_pending = false;

// Last published screen ID (for change detection)
static char g_last_published[16] = {0};

void mqtt_screen_init() {
		snprintf(g_command_topic, sizeof(g_command_topic), "%s/screen/set", mqtt_manager.baseTopic());
		snprintf(g_state_topic, sizeof(g_state_topic), "%s/screen/state", mqtt_manager.baseTopic());
		g_pending = false;
		g_last_published[0] = '\0';
		LOGI(TAG, "Init: cmd=%s state=%s", g_command_topic, g_state_topic);
}

void mqtt_screen_on_connected() {
		mqtt_manager.subscribe(g_command_topic);

		// Publish current screen state
		const char* current = display_manager_get_current_screen_id();
		if (current) {
				mqtt_manager.publish(g_state_topic, current, true);
				strlcpy(g_last_published, current, sizeof(g_last_published));
		}
}

void mqtt_screen_on_message(const char* topic, const uint8_t* payload, unsigned int length) {
		if (!topic || !g_command_topic[0]) return;
		if (strcmp(topic, g_command_topic) != 0) return;

		char screen_id[16];
		size_t copy_len = length < sizeof(screen_id) - 1 ? length : sizeof(screen_id) - 1;
		memcpy(screen_id, payload, copy_len);
		screen_id[copy_len] = '\0';

		portENTER_CRITICAL(&g_mux);
		strlcpy(g_pending_screen, screen_id, sizeof(g_pending_screen));
		g_pending = true;
		portEXIT_CRITICAL(&g_mux);

		LOGI(TAG, "Command received: %s", screen_id);
}

void mqtt_screen_loop() {
		// Process pending HA command
		if (g_pending) {
				char target[16];
				portENTER_CRITICAL(&g_mux);
				strlcpy(target, g_pending_screen, sizeof(target));
				g_pending = false;
				portEXIT_CRITICAL(&g_mux);

				if (target[0]) {
						const char* current = display_manager_get_current_screen_id();
						bool is_same = current && strcmp(current, target) == 0;

						if (!is_same) {
								display_manager_show_screen(target, nullptr);
						}
						// Always wake + reset inactivity (mimics a tap)
						screen_saver_manager_notify_activity(true);
				}
		}

		// Publish screen state on change
		const char* current = display_manager_get_current_screen_id();
		if (current && strcmp(current, g_last_published) != 0) {
				if (mqtt_manager.connected()) {
						mqtt_manager.publish(g_state_topic, current, true);
						strlcpy(g_last_published, current, sizeof(g_last_published));
				}
		}
}

#endif // HAS_MQTT && HAS_DISPLAY
