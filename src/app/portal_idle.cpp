#include "portal_idle.h"

#include "log_manager.h"
#include "ota_activity.h"
#include "power_manager.h"
#include "web_portal_state.h"

#include <Arduino.h>
#include <atomic>

static std::atomic<unsigned long> g_last_activity_ms{0};
static uint16_t g_timeout_seconds = 0;
static uint32_t g_sleep_seconds = 0;
static PowerMode g_mode = PowerMode::AlwaysOn;
static std::atomic<bool> g_config_upload_in_progress{false};
static bool g_sleep_pending = false;
static uint32_t g_sleep_at_ms = 0;
static uint32_t g_requested_sleep_seconds = 0;

void portal_idle_init() {
		g_last_activity_ms = millis();
}

void portal_idle_notify_activity() {
		g_last_activity_ms = millis();
}

void portal_idle_set_timeout_seconds(uint16_t seconds, uint32_t sleep_seconds) {
		g_timeout_seconds = seconds;
		g_sleep_seconds = sleep_seconds;
}

void portal_idle_set_mode(PowerMode mode) {
		g_mode = mode;
}

void portal_idle_set_config_upload_in_progress(bool in_progress) {
		g_config_upload_in_progress = in_progress;
}

bool portal_idle_request_sleep(uint32_t seconds, uint32_t grace_ms) {
		if (g_sleep_pending || g_config_upload_in_progress.load()) return false;
		if (!ota_activity_try_begin_network_transition()) return false;
		if (g_config_upload_in_progress.load()) {
				ota_activity_finish_network_transition();
				return false;
		}
		g_requested_sleep_seconds = seconds;
		g_sleep_at_ms = millis() + grace_ms;
		g_sleep_pending = true;
		return true;
}

void portal_idle_loop() {
		if (g_sleep_pending) {
				if ((int32_t)(millis() - g_sleep_at_ms) >= 0) {
						g_sleep_pending = false;
						power_manager_sleep_for(g_requested_sleep_seconds);
				}
				return;
		}
		if (g_timeout_seconds == 0) return;

		const bool mode_allows_sleep = (g_mode == PowerMode::Config || g_mode == PowerMode::Ap);
		if (!mode_allows_sleep) return;

		if (web_portal_ota_in_progress()) return;
		if (g_config_upload_in_progress.load()) return;

		const unsigned long last_activity_ms = g_last_activity_ms.load();
		const unsigned long now = millis();
		if (last_activity_ms == 0) {
				g_last_activity_ms = now;
				return;
		}

		const unsigned long idle_ms = now - last_activity_ms;
		if (idle_ms >= (unsigned long)g_timeout_seconds * 1000UL) {
				if (!portal_idle_request_sleep(g_sleep_seconds, 0)) return;
				LOGI("Portal", "Idle timeout reached (%us)", (unsigned)g_timeout_seconds);
		}
}
