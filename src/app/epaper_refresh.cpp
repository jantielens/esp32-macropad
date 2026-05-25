#include "epaper_refresh.h"

#if HAS_EPAPER

#include "config_manager.h"
#include "epaper_crc32.h"
#include "epaper_driver.h"
#include "log_manager.h"
#include "power_manager.h"

#include <Arduino.h>
#include <esp_attr.h>
#include <esp_task_wdt.h>
#include <time.h>

// Anything earlier than 2024-01-01 is treated as "clock not yet synced".
static constexpr uint32_t kMinValidEpoch = 1704067200U;

// Persisted across deep sleep so the portal status card can show a meaningful
// "last refresh" timestamp after the device wakes into config mode. Cleared
// on cold boot / power loss.
RTC_DATA_ATTR static uint32_t g_last_refresh_unix = 0;
RTC_DATA_ATTR static uint32_t g_refresh_count = 0;

static EpaperRefreshOutcome s_last_outcome = {EpaperRefreshResult::Disabled, 0, 0, 0, 0};

EpaperRefreshOutcome epaper_refresh_run(DeviceConfig* config, bool force) {
		EpaperRefreshOutcome out = {EpaperRefreshResult::Disabled, 0, 0, 0, 0};
		const uint32_t t0 = millis();

		if (!config || strlen(config->epaper_url) == 0) {
				LOGW("Epaper", "Refresh skipped: no URL configured");
				s_last_outcome = out;
				return out;
		}

		// Compare sidecar CRC against last-known value to avoid unnecessary
		// 10-30s panel refreshes. Skipped on force=true (portal "Refresh now")
		// to avoid wasting 1-5s of sidecar retries when the user explicitly
		// asks for a redraw.
		EpaperCrcFetchResult crc_fetch = {0, 0};
		if (!force) {
				crc_fetch = epaper_crc32_fetch_sidecar(config->epaper_url);
		}
		const uint32_t fresh_crc = crc_fetch.crc;
		out.crc_used = fresh_crc;
		out.sidecar_http_status = crc_fetch.http_status;
		// Treat HTTP status 0 (begin/connect failure) and negative (HTTPClient
		// error codes such as HTTPC_ERROR_CONNECTION_REFUSED) as transport-level
		// failures, distinct from "server returned a non-200 response".
		const bool sidecar_transport_failed = !force && crc_fetch.http_status <= 0;

		if (!force && fresh_crc != 0 && fresh_crc == config->epaper_last_crc32) {
				out.result = EpaperRefreshResult::Skipped;
				out.elapsed_ms = millis() - t0;
				LOGI("Epaper", "Refresh skipped: CRC unchanged (%08x)", (unsigned)fresh_crc);
				s_last_outcome = out;
				return out;
		}

		if (!epaper_driver_begin()) {
				out.result = EpaperRefreshResult::FailedDraw;
				out.elapsed_ms = millis() - t0;
				s_last_outcome = out;
				return out;
		}

		epaper_driver_set_rotation(config->epaper_rotation);

		// drawImage is the long pole — disable the task watchdog for this thread
		// while it runs to avoid spurious resets during the panel refresh.
		const bool wdt_was_attached = (esp_task_wdt_status(nullptr) == ESP_OK);
		if (wdt_was_attached) {
				esp_task_wdt_delete(nullptr);
		}

		const bool drew = epaper_driver_draw_url(config->epaper_url);

		if (drew) {
				epaper_driver_display();
		}

		if (wdt_was_attached) {
				esp_task_wdt_add(nullptr);
		}

		out.battery_mv = epaper_driver_battery_mv();
		epaper_driver_sleep();

		if (!drew) {
				// If the sidecar fetch also failed at the transport layer, the most
				// likely root cause is network/server unreachability rather than a
				// panel-side problem; surface that to the portal so users can tell
				// "server unreachable" apart from "server returned garbage image".
				out.result = sidecar_transport_failed
					? EpaperRefreshResult::FailedFetch
					: EpaperRefreshResult::FailedDraw;
				out.elapsed_ms = millis() - t0;
				s_last_outcome = out;
				return out;
		}

		// Persist CRC so subsequent wakes can short-circuit when unchanged.
		if (fresh_crc != 0 && fresh_crc != config->epaper_last_crc32) {
				config->epaper_last_crc32 = fresh_crc;
				config_manager_save(config);
		}

		out.result = EpaperRefreshResult::Updated;
		out.elapsed_ms = millis() - t0;
		LOGI("Epaper", "Refresh complete: %ums, battery=%umV", (unsigned)out.elapsed_ms, (unsigned)out.battery_mv);
		// Only record the timestamp if NTP has actually synced; otherwise leave
		// the previous (possibly persisted) value untouched so we don't poison
		// it with a 1970 epoch.
		const time_t now = time(nullptr);
		if (now >= (time_t)kMinValidEpoch) {
				g_last_refresh_unix = (uint32_t)now;
		}
		++g_refresh_count;
		s_last_outcome = out;
		return out;
}

uint32_t epaper_refresh_last_unix() {
		return g_last_refresh_unix;
}

EpaperRefreshOutcome epaper_refresh_last_outcome() {
		return s_last_outcome;
}

uint32_t epaper_refresh_get_count() {
		return g_refresh_count;
}

#endif // HAS_EPAPER
