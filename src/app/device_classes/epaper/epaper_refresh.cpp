#include "epaper_refresh.h"

#if HAS_EPAPER

#include "config_manager.h"
#include "epaper_config.h"
#include "epaper_crc32.h"
#include "epaper_driver.h"
#if defined(BOARD_RETERMINAL_E1003)
#include "epaper_next_client.h"
#include "epaper_sd_cache.h"
#endif
#include "epaper_overlay.h"
#include "epaper_screens.h"
#include "log_manager.h"
#include "power_manager.h"

#include <Arduino.h>
#include <esp_attr.h>
#include <esp_task_wdt.h>
#include <time.h>

// Persisted across deep sleep so the portal status card can show a meaningful
// "last refresh" timestamp after the device wakes into config mode. Cleared
// on cold boot / power loss.
RTC_DATA_ATTR static uint32_t g_last_refresh_unix = 0;
RTC_DATA_ATTR static uint32_t g_refresh_count = 0;
#if defined(BOARD_RETERMINAL_E1003)
RTC_DATA_ATTR static EpaperCurrentFingerprint g_service_fingerprint = {};
#endif

static EpaperRefreshOutcome s_last_outcome = {EpaperRefreshResult::Disabled, 0, 0, 0, 0, 0};

static EpaperRefreshOutcome epaper_refresh_run_url(DeviceConfig* config, const char* image_url,
		bool force, bool allow_crc, bool persist_crc) {
		EpaperRefreshOutcome out = {EpaperRefreshResult::Disabled, 0, 0, 0, 0, 0};
		const uint32_t t0 = millis();

		if (!config || !image_url || strlen(image_url) == 0) {
				LOGW("Epaper", "Refresh skipped: no URL configured");
				s_last_outcome = out;
				return out;
		}

		// Compare sidecar CRC against last-known value to avoid unnecessary
		// 10-30s panel refreshes. Skipped on force=true (portal "Refresh now")
		// to avoid wasting 1-5s of sidecar retries when the user explicitly
		// asks for a redraw, and only attempted when the user has opted in via
		// the "Use CRC32 change detection" setting (default off).
		EpaperCrcFetchResult crc_fetch = {0, 0, 0};
		if (allow_crc && !force && g_epaper_config.epaper_crc32_enabled) {
				crc_fetch = epaper_crc32_fetch_sidecar(image_url);
		}
		const uint32_t fresh_crc = crc_fetch.crc;
		out.crc_used = fresh_crc;
		out.sidecar_http_status = crc_fetch.http_status;
		out.crc_retry_count = crc_fetch.attempts;
		// Treat HTTP status 0 (begin/connect failure) and negative (HTTPClient
		// error codes such as HTTPC_ERROR_CONNECTION_REFUSED) as transport-level
		// failures, distinct from "server returned a non-200 response".
		const bool sidecar_transport_failed =
				allow_crc && !force && g_epaper_config.epaper_crc32_enabled && crc_fetch.http_status <= 0;

		if (!force && fresh_crc != 0 && fresh_crc == g_epaper_config.epaper_last_crc32) {
				out.result = EpaperRefreshResult::Skipped;
				// Even on the skip path we still want a fresh battery reading so HA
				// can chart it across all wakes. readBattery() needs the TPS65186
				// rails up, so power-cycle the panel briefly (no waveform drive,
				// so still <100 ms and minimal current draw).
				const bool panel_ok = epaper_driver_begin();
				if (panel_ok) {
								out.battery_mv = epaper_driver_battery_mv();
								epaper_driver_sleep();
				}
				out.elapsed_ms = millis() - t0;
				if (panel_ok) {
								LOGI("Epaper", "Refresh skipped: CRC unchanged (%08x), battery=%umV",
										 (unsigned)fresh_crc, (unsigned)out.battery_mv);
				} else {
								LOGW("Epaper", "Refresh skipped: CRC unchanged (%08x), battery read unavailable (panel init failed)",
										 (unsigned)fresh_crc);
				}
				out.elapsed_ms = millis() - t0;
				s_last_outcome = out;
				return out;
		}

		// Render the dashboard image in the panel's native orientation. The
		// configured `epaper_rotation` only affects on-device overlays and
		// status screens — the image itself is assumed to be delivered
		// pre-rotated by the publisher.
		// Ensure the driver object is initialized before we render this frame.
		// Earlier wake stages may have already initialized it, so begin() is
		// usually an inexpensive no-op in steady state.
		if (!epaper_driver_begin()) {
				LOGW("Epaper", "epaper_driver_begin() failed; cannot refresh image");
				out.result = EpaperRefreshResult::FailedDraw;
				out.elapsed_ms = millis() - t0;
				s_last_outcome = out;
				return out;
		}
		epaper_driver_set_rotation(0);
		// Earlier wake stages (boot splash, low-battery screen) may have left
		// pixels in the framebuffer. Wipe before drawing the new image so the
		// previous content doesn't bleed through at the configured rotation.
		epaper_driver_clear();
		// Read battery BEFORE drawImage so the sample reflects WiFi-only current
		// (~80 mA) rather than the panel waveform drive (~200 mA), which sags
		// the cell and produces an artificially low SoC estimate.
		out.battery_mv = epaper_driver_battery_mv();

		// drawImage is the long pole — disable the task watchdog for this thread
		// while it runs to avoid spurious resets during the panel refresh.
		const bool wdt_was_attached = (esp_task_wdt_status(nullptr) == ESP_OK);
		if (wdt_was_attached) {
				esp_task_wdt_delete(nullptr);
		}

		// Serve from / write to the SD image cache per the user's setting. On a
		// cache hit this lets draw_url() skip the multi-second HTTP body download.
		epaper_driver_set_sd_cache_enabled(g_epaper_config.epaper_sd_cache_enabled);

		const bool image_ready = epaper_driver_draw_url(image_url);
		bool drew = image_ready;

		if (drew) {
				// Switch to the user's configured rotation so the overlay lands in
				// the correct screen corner regardless of how the image was framed.
				epaper_driver_set_rotation(g_epaper_config.epaper_rotation);
				// Composite the status overlay on top of the dashboard image before
				// pushing the frame. Pure framebuffer draws — no panel waveform yet.
				epaper_overlay_render(out.battery_mv, (uint32_t)(millis() - t0));
				drew = epaper_driver_display();
				// Write a freshly downloaded image back to the SD cache now that the
				// frame is on screen, keeping the slow write off the wake-to-visible
				// path. No-op on a cache hit or when SD caching is unsupported/off.
				if (drew) epaper_driver_cache_flush();
				else epaper_sd_cache_discard_pending();
		}

		if (wdt_was_attached) {
				esp_task_wdt_add(nullptr);
		}

		epaper_driver_sleep();

		if (!drew) {
				// If the sidecar fetch also failed at the transport layer, the most
				// likely root cause is network/server unreachability rather than a
				// panel-side problem; surface that to the portal so users can tell
				// "server unreachable" apart from "server returned garbage image".
				out.result = !image_ready && sidecar_transport_failed
					? EpaperRefreshResult::FailedFetch
					: EpaperRefreshResult::FailedDraw;
				// On a silent timer wake, keep whatever image is already on the
				// bistable panel rather than replacing the (presumably good) last
				// dashboard with an error screen — a transient fetch hiccup should
				// not blank a working display. On cold boot or button wake the user
				// is actively looking at the device and expects feedback, so push a
				// visible error screen instead. The portal status card reflects the
				// failure in all cases regardless of what the panel shows.
				const bool is_timer_wake =
						power_manager_is_deep_sleep_wake() && !epaper_button_is_button_wake();
				if (!is_timer_wake && epaper_driver_begin()) {
						epaper_driver_set_rotation(g_epaper_config.epaper_rotation);
						const char* detail = sidecar_transport_failed
								? "Network or server unreachable"
								: "Image fetch or decode failed";
						epaper_screen_error(detail, config->duty_cycle_wake_seconds);
						epaper_driver_display();
						epaper_driver_sleep();
				} else if (is_timer_wake) {
						LOGW("Epaper", "Refresh failed on timer wake; keeping existing image on panel");
				}
				out.elapsed_ms = millis() - t0;
				s_last_outcome = out;
				return out;
		}

		// Persist CRC so subsequent wakes can short-circuit when unchanged.
		if (persist_crc && fresh_crc != 0 && fresh_crc != g_epaper_config.epaper_last_crc32) {
				g_epaper_config.epaper_last_crc32 = fresh_crc;
				epaper_config_persist_crc(fresh_crc);
		}

		out.result = EpaperRefreshResult::Updated;
		out.elapsed_ms = millis() - t0;
		LOGI("Epaper", "Refresh complete: %ums, battery=%umV", (unsigned)out.elapsed_ms, (unsigned)out.battery_mv);
		// Only record the timestamp if NTP has actually synced; otherwise leave
		// the previous (possibly persisted) value untouched so we don't poison
		// it with a 1970 epoch.
		const time_t now = time(nullptr);
		if (now >= (time_t)kEpaperMinValidEpoch) {
				g_last_refresh_unix = (uint32_t)now;
		}
		++g_refresh_count;
		s_last_outcome = out;
		return out;
}

#if defined(BOARD_RETERMINAL_E1003)
static EpaperRefreshOutcome epaper_refresh_run_service(DeviceConfig* config) {
		EpaperRefreshOutcome out = {EpaperRefreshResult::Disabled, 0, 0, 0, 0, 0};
		const uint32_t started = millis();
		EpaperNextPayload payload = epaper_next_client_fetch(
				g_epaper_config.service_url, g_epaper_config.service_token,
				g_service_fingerprint, g_epaper_config.epaper_sd_cache_enabled);
		out.crc_used = payload.content_crc32;
		if (payload.result == EpaperNextResult::Keep) {
				out.result = EpaperRefreshResult::Skipped;
				out.elapsed_ms = millis() - started;
				s_last_outcome = out;
				LOGI("Epaper", "Service returned 204 keep");
				return out;
		}
		if (payload.result != EpaperNextResult::Show) {
				out.result = payload.result == EpaperNextResult::FailedContent
						? EpaperRefreshResult::FailedDraw : EpaperRefreshResult::FailedFetch;
				out.elapsed_ms = millis() - started;
				s_last_outcome = out;
				LOGW("Epaper", "Service refresh failed (result=%u)", (unsigned)payload.result);
				return out;
		}

		if (!epaper_driver_begin()) {
				epaper_next_payload_release(&payload);
				out.result = EpaperRefreshResult::FailedDraw;
				out.elapsed_ms = millis() - started;
				s_last_outcome = out;
				return out;
		}
		epaper_driver_set_rotation(0);
		epaper_driver_clear();
		out.battery_mv = epaper_driver_battery_mv();
		const bool wdt_was_attached = esp_task_wdt_status(nullptr) == ESP_OK;
		if (wdt_was_attached) esp_task_wdt_delete(nullptr);

		epaper_driver_set_sd_cache_enabled(g_epaper_config.epaper_sd_cache_enabled);
		bool drew = epaper_driver_draw_service_blob(
				payload.data, payload.len, payload.media_type[0] ? payload.media_type : nullptr,
				payload.prepared_data, payload.prepared_len);
		if (!drew && payload.from_cache) {
				epaper_sd_cache_remove(payload.content_crc32);
				epaper_next_payload_release(&payload);
				LOGW("Epaper", "Cached service image failed decode; retrying without cache");
				payload = epaper_next_client_fetch(
						g_epaper_config.service_url, g_epaper_config.service_token,
						g_service_fingerprint, false /*cache_enabled*/, 1 /*max_cycles*/);
				out.crc_used = payload.content_crc32;
				if (payload.result == EpaperNextResult::Show) {
						drew = epaper_driver_draw_service_blob(
								payload.data, payload.len,
								payload.media_type[0] ? payload.media_type : nullptr,
								payload.prepared_data, payload.prepared_len);
				}
		}
		if (drew) {
			epaper_driver_set_rotation(g_epaper_config.epaper_rotation);
			epaper_overlay_render(out.battery_mv, millis() - started);
			if (!payload.from_cache && g_epaper_config.epaper_sd_cache_enabled) {
					epaper_sd_cache_stage_pending(payload.content_crc32, payload.data, payload.len);
					payload.data = nullptr;
					payload.len = 0;
			}
			drew = epaper_driver_display();
			if (drew) {
				epaper_driver_cache_flush();
				g_service_fingerprint.valid = true;
				strlcpy(g_service_fingerprint.image_key, payload.image_key,
						sizeof(g_service_fingerprint.image_key));
				g_service_fingerprint.content_crc32 = payload.content_crc32;
			} else {
				epaper_sd_cache_discard_pending();
			}
		}
		epaper_next_payload_release(&payload);
		if (wdt_was_attached) esp_task_wdt_add(nullptr);
		epaper_driver_sleep();

		out.result = drew ? EpaperRefreshResult::Updated : EpaperRefreshResult::FailedDraw;
		out.elapsed_ms = millis() - started;
		if (drew) {
				const time_t now = time(nullptr);
				if (now >= (time_t)kEpaperMinValidEpoch) g_last_refresh_unix = (uint32_t)now;
				++g_refresh_count;
				LOGI("Epaper", "Service refresh complete: %ums, CRC=%08x",
						(unsigned)out.elapsed_ms, (unsigned)out.crc_used);
		}
		s_last_outcome = out;
		return out;
}
#endif

EpaperRefreshOutcome epaper_refresh_run(DeviceConfig* config, bool force) {
		if (epaper_source_uses_service(g_epaper_config.source_mode)) {
#if defined(BOARD_RETERMINAL_E1003)
				(void)force;
				return epaper_refresh_run_service(config);
#else
				EpaperRefreshOutcome unsupported = {
						EpaperRefreshResult::Disabled, 0, 0, 0, 0, 0};
				return unsupported;
#endif
		}
		return epaper_refresh_run_url(config, g_epaper_config.epaper_url,
				force, true /*allow_crc*/, true /*persist_crc*/);
}

EpaperRefreshOutcome epaper_refresh_show_url(DeviceConfig* config, const char* image_url) {
		return epaper_refresh_run_url(config, image_url,
				true /*force*/, false /*allow_crc*/, false /*persist_crc*/);
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
