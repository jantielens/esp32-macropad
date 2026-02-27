#include "portal_idle.h"

#include "log_manager.h"
#include "power_manager.h"
#include "web_portal_state.h"

#include <Arduino.h>

static unsigned long g_last_activity_ms = 0;
static uint16_t g_timeout_seconds = 0;
static PowerMode g_mode = PowerMode::AlwaysOn;
static bool g_config_upload_in_progress = false;

void portal_idle_init() {
		g_last_activity_ms = millis();
}

void portal_idle_notify_activity() {
		g_last_activity_ms = millis();
}

void portal_idle_set_timeout_seconds(uint16_t seconds) {
		g_timeout_seconds = seconds;
}

void portal_idle_set_mode(PowerMode mode) {
		g_mode = mode;
}

void portal_idle_set_config_upload_in_progress(bool in_progress) {
		g_config_upload_in_progress = in_progress;
}

void portal_idle_loop() {
		if (g_timeout_seconds == 0) return;

		const bool mode_allows_sleep = (g_mode == PowerMode::Config || g_mode == PowerMode::Ap);
		if (!mode_allows_sleep) return;

		if (web_portal_ota_in_progress()) return;
		if (g_config_upload_in_progress) return;

		const unsigned long now = millis();
		if (g_last_activity_ms == 0) {
				g_last_activity_ms = now;
				return;
		}

		const unsigned long idle_ms = now - g_last_activity_ms;
		if (idle_ms >= (unsigned long)g_timeout_seconds * 1000UL) {
				LOGI("Portal", "Idle timeout reached (%us)", (unsigned)g_timeout_seconds);
				power_manager_sleep_for(g_timeout_seconds);
		}
}
