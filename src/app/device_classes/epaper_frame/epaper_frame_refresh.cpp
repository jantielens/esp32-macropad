#include "epaper_frame_refresh.h"

#if IS_EPAPER_FRAME

#include "config_manager.h"
#include "epaper_frame_config.h"
#include "epaper_frame_crc32.h"
#include "epaper_frame_driver.h"
#if defined(BOARD_RETERMINAL_E1003_FRAME)
#include "epaper_frame_next_client.h"
#include "epaper_frame_offline_queue.h"
#include "epaper_frame_sd_cache.h"
#include "epaper_frame_timing.h"
#include "epaper_frame_transport_crc32.h"
#endif
#include "epaper_frame_overlay.h"
#include "epaper_frame_screens.h"
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
#if defined(BOARD_RETERMINAL_E1003_FRAME)
RTC_DATA_ATTR static EpaperCurrentFingerprint g_service_fingerprint = {};
#endif

static EpaperRefreshOutcome s_last_outcome = {EpaperRefreshResult::Disabled, 0, 0, 0, 0, 0};

static EpaperRefreshOutcome epaper_frame_refresh_run_url(DeviceConfig* config, const char* image_url,
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
		if (allow_crc && !force && g_epaper_config.epaper_frame_crc32_enabled) {
				crc_fetch = epaper_frame_crc32_fetch_sidecar(image_url);
		}
		const uint32_t fresh_crc = crc_fetch.crc;
		out.crc_used = fresh_crc;
		out.sidecar_http_status = crc_fetch.http_status;
		out.crc_retry_count = crc_fetch.attempts;
		// Treat HTTP status 0 (begin/connect failure) and negative (HTTPClient
		// error codes such as HTTPC_ERROR_CONNECTION_REFUSED) as transport-level
		// failures, distinct from "server returned a non-200 response".
		const bool sidecar_transport_failed =
				allow_crc && !force && g_epaper_config.epaper_frame_crc32_enabled && crc_fetch.http_status <= 0;

		if (!force && fresh_crc != 0 && fresh_crc == g_epaper_config.epaper_frame_last_crc32) {
				out.result = EpaperRefreshResult::Skipped;
				// Even on the skip path we still want a fresh battery reading so HA
				// can chart it across all wakes. readBattery() needs the TPS65186
				// rails up, so power-cycle the panel briefly (no waveform drive,
				// so still <100 ms and minimal current draw).
				const bool panel_ok = epaper_frame_driver_begin();
				if (panel_ok) {
								out.battery_mv = epaper_frame_driver_battery_mv();
								epaper_frame_driver_sleep();
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
		// configured `epaper_frame_rotation` only affects on-device overlays and
		// status screens — the image itself is assumed to be delivered
		// pre-rotated by the publisher.
		// Ensure the driver object is initialized before we render this frame.
		// Earlier wake stages may have already initialized it, so begin() is
		// usually an inexpensive no-op in steady state.
		if (!epaper_frame_driver_begin()) {
				LOGW("Epaper", "epaper_frame_driver_begin() failed; cannot refresh image");
				out.result = EpaperRefreshResult::FailedDraw;
				out.elapsed_ms = millis() - t0;
				s_last_outcome = out;
				return out;
		}
		epaper_frame_driver_set_rotation(0);
		// Earlier wake stages (boot splash, low-battery screen) may have left
		// pixels in the framebuffer. Wipe before drawing the new image so the
		// previous content doesn't bleed through at the configured rotation.
		epaper_frame_driver_clear();
		// Read battery BEFORE drawImage so the sample reflects WiFi-only current
		// (~80 mA) rather than the panel waveform drive (~200 mA), which sags
		// the cell and produces an artificially low SoC estimate.
		out.battery_mv = epaper_frame_driver_battery_mv();

		// drawImage is the long pole — disable the task watchdog for this thread
		// while it runs to avoid spurious resets during the panel refresh.
		const bool wdt_was_attached = (esp_task_wdt_status(nullptr) == ESP_OK);
		if (wdt_was_attached) {
				esp_task_wdt_delete(nullptr);
		}

		// Serve from / write to the SD image cache per the user's setting. On a
		// cache hit this lets draw_url() skip the multi-second HTTP body download.
		epaper_frame_driver_set_sd_cache_enabled(g_epaper_config.epaper_frame_sd_cache_enabled);

		const bool image_ready = epaper_frame_driver_draw_url(image_url);
		bool drew = image_ready;

		if (drew) {
				// Switch to the user's configured rotation so the overlay lands in
				// the correct screen corner regardless of how the image was framed.
				epaper_frame_driver_set_rotation(g_epaper_config.epaper_frame_rotation);
				// Composite the status overlay on top of the dashboard image before
				// pushing the frame. Pure framebuffer draws — no panel waveform yet.
				epaper_frame_overlay_render(out.battery_mv, (uint32_t)(millis() - t0));
				drew = epaper_frame_driver_display();
				// Write a freshly downloaded image back to the SD cache now that the
				// frame is on screen, keeping the slow write off the wake-to-visible
				// path. No-op on a cache hit or when SD caching is unsupported/off.
				if (drew) epaper_frame_driver_cache_flush();
				else epaper_frame_sd_cache_discard_pending();
		}

		if (wdt_was_attached) {
				esp_task_wdt_add(nullptr);
		}

		epaper_frame_driver_sleep();

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
						power_manager_is_deep_sleep_wake() && !epaper_frame_button_is_button_wake();
				if (!is_timer_wake && epaper_frame_driver_begin()) {
						epaper_frame_driver_set_rotation(g_epaper_config.epaper_frame_rotation);
						const char* detail = sidecar_transport_failed
								? "Network or server unreachable"
								: "Image fetch or decode failed";
						epaper_frame_screen_error(detail, config->duty_cycle_wake_seconds);
						epaper_frame_driver_display();
						epaper_frame_driver_sleep();
				} else if (is_timer_wake) {
						LOGW("Epaper", "Refresh failed on timer wake; keeping existing image on panel");
				}
				out.elapsed_ms = millis() - t0;
				s_last_outcome = out;
				return out;
		}

		// Persist CRC so subsequent wakes can short-circuit when unchanged.
		if (persist_crc && fresh_crc != 0 && fresh_crc != g_epaper_config.epaper_frame_last_crc32) {
				g_epaper_config.epaper_frame_last_crc32 = fresh_crc;
				epaper_frame_config_persist_crc(fresh_crc);
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

#if defined(BOARD_RETERMINAL_E1003_FRAME)
static uint32_t service_remaining_timeout(uint32_t started,
		uint32_t timeout_ms) {
		if (timeout_ms == 0) return 0;
		const uint32_t elapsed = millis() - started;
		return elapsed < timeout_ms ? timeout_ms - elapsed : 0;
}

static uint32_t service_request_timeout(uint32_t started,
		uint32_t overall_timeout_ms, uint32_t per_image_timeout_ms) {
		if (overall_timeout_ms == 0) return per_image_timeout_ms;
		const uint32_t remaining = service_remaining_timeout(started, overall_timeout_ms);
		return per_image_timeout_ms == 0 || remaining < per_image_timeout_ms
				? remaining : per_image_timeout_ms;
}

static bool epaper_frame_refresh_draw_service_payload(
		EpaperNextPayload* payload, EpaperRefreshOutcome* out,
		uint32_t started) {
		if (!payload || !out || payload->result != EpaperNextResult::Show ||
				!epaper_frame_driver_begin()) {
				return false;
		}
		epaper_frame_driver_set_rotation(0);
		epaper_frame_driver_clear();
		out->battery_mv = epaper_frame_driver_battery_mv();
		const bool wdt_was_attached = esp_task_wdt_status(nullptr) == ESP_OK;
		if (wdt_was_attached) esp_task_wdt_delete(nullptr);

		epaper_frame_driver_set_sd_cache_enabled(
				g_epaper_config.epaper_frame_sd_cache_enabled);
		bool drew = epaper_frame_driver_draw_service_blob(
				payload->data, payload->len,
				payload->media_type[0] ? payload->media_type : nullptr,
				payload->prepared_data, payload->prepared_len);
		if (drew) {
				epaper_frame_driver_set_rotation(
						g_epaper_config.epaper_frame_rotation);
				epaper_frame_overlay_render(out->battery_mv, millis() - started);
				if (!payload->from_cache &&
						g_epaper_config.epaper_frame_sd_cache_enabled) {
						epaper_frame_sd_cache_stage_pending(
								payload->content_crc32, payload->data, payload->len);
						payload->data = nullptr;
						payload->len = 0;
				}
				drew = epaper_frame_driver_display();
				if (drew) {
						epaper_frame_driver_cache_flush();
						g_service_fingerprint.valid = true;
						strlcpy(g_service_fingerprint.image_key,
								payload->image_key,
								sizeof(g_service_fingerprint.image_key));
						g_service_fingerprint.content_crc32 =
								payload->content_crc32;
				} else {
						epaper_frame_sd_cache_discard_pending();
				}
		}
		if (wdt_was_attached) esp_task_wdt_add(nullptr);
		epaper_frame_driver_sleep();
		return drew;
}

static void epaper_frame_refresh_finish_service(
		EpaperRefreshOutcome* out, uint32_t started) {
		out->elapsed_ms = millis() - started;
		if (out->result == EpaperRefreshResult::Updated) {
				const time_t now = time(nullptr);
				if (now >= (time_t)kEpaperMinValidEpoch) {
						g_last_refresh_unix = (uint32_t)now;
				}
				++g_refresh_count;
				LOGI("Epaper", "Service refresh complete: %ums, CRC=%08x",
						(unsigned)out->elapsed_ms, (unsigned)out->crc_used);
		}
		s_last_outcome = *out;
}

static void epaper_frame_queue_batch_extras(
		const EpaperBatchManifest& manifest, uint32_t sync_started,
		uint32_t overall_timeout_ms, uint32_t per_image_timeout_ms) {
		epaper_frame_offline_queue_begin_sync();
		epaper_frame_sd_cache_begin_batch();
		for (uint8_t index = 1; index < manifest.count; ++index) {
				const uint32_t remaining =
						service_request_timeout(sync_started, overall_timeout_ms,
								per_image_timeout_ms);
				if (overall_timeout_ms > 0 && remaining == 0) {
						LOGW("Epaper", "Batch prefetch stopped at wake budget");
						break;
				}
				EpaperNextPayload queued =
						epaper_frame_next_client_fetch_batch_entry(
								g_epaper_config.service_url,
								g_epaper_config.service_token,
								manifest.entries[index], true, remaining);
				if (queued.result != EpaperNextResult::Show) {
						epaper_frame_next_payload_release(&queued);
						LOGW("Epaper", "Batch prefetch stopped at entry %u",
								(unsigned)index);
						break;
				}
				bool cached = queued.from_cache;
				if (!cached) {
						cached = epaper_frame_sd_cache_store(
								queued.content_crc32, queued.data, queued.len);
				}
				if (!cached) {
						epaper_frame_next_payload_release(&queued);
						LOGW("Epaper", "Batch prefetch cache write failed");
						break;
				}
				EpaperOfflineQueueEntry entry = {};
				strlcpy(entry.image_key, manifest.entries[index].image_key,
						sizeof(entry.image_key));
				strlcpy(entry.media_type, manifest.entries[index].media_type,
						sizeof(entry.media_type));
				entry.content_crc32 = manifest.entries[index].content_crc32;
				entry.content_length = manifest.entries[index].content_length;
				const bool appended =
						epaper_frame_offline_queue_append_validated(entry);
				epaper_frame_next_payload_release(&queued);
				if (!appended) break;
		}
		epaper_frame_sd_cache_end_batch();
		LOGI("Epaper", "Offline queue ready with %u entries",
				(unsigned)epaper_frame_offline_queue_count());
}

static EpaperRefreshOutcome epaper_frame_refresh_run_service(
		DeviceConfig* /*config*/, uint32_t fetch_timeout_ms,
		uint32_t overall_timeout_ms) {
		EpaperRefreshOutcome out = {
				EpaperRefreshResult::Disabled, 0, 0, 0, 0, 0};
		const uint32_t started = millis();
		const bool batch_enabled =
				g_epaper_config.offline_refreshes_between_syncs > 0 &&
				g_epaper_config.epaper_frame_sd_cache_enabled;
		EpaperBatchManifest* manifest = nullptr;
		EpaperNextPayload payload = {};
		payload.result = EpaperNextResult::FailedFetch;
		bool batch_first = false;

		if (batch_enabled) {
				epaper_frame_offline_queue_invalidate();
				uint8_t batch_count = 0;
				if (epaper_frame_offline_batch_count(
						g_epaper_config.offline_refreshes_between_syncs,
						&batch_count)) {
						const uint32_t manifest_timeout =
								service_request_timeout(started, overall_timeout_ms,
										fetch_timeout_ms);
						const EpaperNextResult batch_result =
								overall_timeout_ms > 0 && manifest_timeout == 0
								? EpaperNextResult::FailedFetch :
								epaper_frame_next_client_fetch_batch_manifest(
										g_epaper_config.service_url,
										g_epaper_config.service_token,
										g_service_fingerprint, batch_count,
										&manifest,
										manifest_timeout);
						if (batch_result == EpaperNextResult::Show &&
								manifest && manifest->count > 0) {
								const uint32_t first_timeout =
										service_request_timeout(started, overall_timeout_ms,
												fetch_timeout_ms);
								if (overall_timeout_ms > 0 &&
										first_timeout == 0) {
										payload.result =
												EpaperNextResult::FailedFetch;
								} else {
								payload =
										epaper_frame_next_client_fetch_batch_entry(
												g_epaper_config.service_url,
												g_epaper_config.service_token,
												manifest->entries[0], true,
												first_timeout);
								}
								batch_first =
										payload.result == EpaperNextResult::Show;
						}
				}
				if (!batch_first) {
						epaper_frame_next_payload_release(&payload);
						epaper_frame_batch_manifest_release(manifest);
						manifest = nullptr;
						LOGW("Epaper", "Batch first image unavailable; falling back to /next");
				}
		}

		if (!batch_first) {
				const uint32_t fallback_timeout =
						batch_enabled
						? service_request_timeout(started, overall_timeout_ms,
								fetch_timeout_ms)
						: service_remaining_timeout(started, fetch_timeout_ms);
				if ((batch_enabled ? overall_timeout_ms : fetch_timeout_ms) > 0 && fallback_timeout == 0) {
						payload = {};
						payload.result = EpaperNextResult::FailedFetch;
				} else {
						payload = epaper_frame_next_client_fetch(
						g_epaper_config.service_url,
						g_epaper_config.service_token,
						g_service_fingerprint,
						g_epaper_config.epaper_frame_sd_cache_enabled,
						batch_enabled ? 1 : 2,
						fallback_timeout);
				}
		}
		out.crc_used = payload.content_crc32;
		if (payload.result == EpaperNextResult::Keep) {
				out.result = EpaperRefreshResult::Skipped;
				epaper_frame_next_payload_release(&payload);
				epaper_frame_batch_manifest_release(manifest);
				epaper_frame_refresh_finish_service(&out, started);
				LOGI("Epaper", "Service returned 204 keep");
				return out;
		}
		if (payload.result != EpaperNextResult::Show) {
				out.result = payload.result == EpaperNextResult::FailedContent
						? EpaperRefreshResult::FailedDraw
						: EpaperRefreshResult::FailedFetch;
				epaper_frame_next_payload_release(&payload);
				epaper_frame_batch_manifest_release(manifest);
				epaper_frame_refresh_finish_service(&out, started);
				LOGW("Epaper", "Service refresh failed (result=%u)",
						(unsigned)payload.result);
				return out;
		}

		bool drew = epaper_frame_refresh_draw_service_payload(
				&payload, &out, started);
		bool skipped = false;
		if (!drew && batch_first) {
				if (payload.from_cache) {
						epaper_frame_sd_cache_remove(payload.content_crc32);
				}
				epaper_frame_next_payload_release(&payload);
				epaper_frame_batch_manifest_release(manifest);
				manifest = nullptr;
				batch_first = false;
				LOGW("Epaper", "Batch first image failed to render; falling back to /next");
				const uint32_t fallback_timeout =
						service_request_timeout(started, overall_timeout_ms,
								fetch_timeout_ms);
				if (overall_timeout_ms == 0 || fallback_timeout > 0) {
						payload = epaper_frame_next_client_fetch(
								g_epaper_config.service_url,
								g_epaper_config.service_token,
								g_service_fingerprint,
								g_epaper_config.epaper_frame_sd_cache_enabled,
								1, fallback_timeout);
						out.crc_used = payload.content_crc32;
						if (payload.result == EpaperNextResult::Show) {
								drew = epaper_frame_refresh_draw_service_payload(
										&payload, &out, started);
						} else if (payload.result == EpaperNextResult::Keep) {
								skipped = true;
						}
				}
		} else if (!drew && payload.from_cache && !batch_enabled) {
				epaper_frame_sd_cache_remove(payload.content_crc32);
				epaper_frame_next_payload_release(&payload);
				LOGW("Epaper", "Cached service image failed decode; retrying without cache");
				payload = epaper_frame_next_client_fetch(
						g_epaper_config.service_url,
						g_epaper_config.service_token,
						g_service_fingerprint, false, 1,
						service_remaining_timeout(started, fetch_timeout_ms));
				out.crc_used = payload.content_crc32;
				if (payload.result == EpaperNextResult::Show) {
						drew = epaper_frame_refresh_draw_service_payload(
								&payload, &out, started);
				} else if (payload.result == EpaperNextResult::Keep) {
						skipped = true;
				}
		}
		epaper_frame_next_payload_release(&payload);
		out.result = skipped ? EpaperRefreshResult::Skipped
				: drew ? EpaperRefreshResult::Updated
				: EpaperRefreshResult::FailedDraw;
		if (drew && batch_first && manifest) {
				epaper_frame_queue_batch_extras(
						*manifest, started, overall_timeout_ms, fetch_timeout_ms);
		}
		epaper_frame_batch_manifest_release(manifest);
		epaper_frame_refresh_finish_service(&out, started);
		return out;
}

EpaperRefreshOutcome epaper_frame_refresh_run_offline(DeviceConfig* /*config*/) {
		EpaperRefreshOutcome out = {
				EpaperRefreshResult::FailedFetch, 0, 0, 0, 0, 0};
		const uint32_t started = millis();
		const EpaperOfflineQueueEntry* retained =
				epaper_frame_offline_queue_peek();
		if (!retained) {
				epaper_frame_offline_queue_invalidate();
				epaper_frame_refresh_finish_service(&out, started);
				return out;
		}
		const EpaperOfflineQueueEntry entry = *retained;
		if (!epaper_frame_driver_begin()) {
				epaper_frame_offline_queue_invalidate();
				out.result = EpaperRefreshResult::FailedDraw;
				epaper_frame_refresh_finish_service(&out, started);
				LOGW("Epaper", "Offline queue panel initialization failed");
				return out;
		}
		uint8_t* data = nullptr;
		size_t length = 0;
		const uint32_t cache_started = millis();
		if (!epaper_frame_sd_cache_read(entry.content_crc32, &data, &length) ||
				length != entry.content_length ||
				epaper_frame_transport_crc32(data, length) !=
						entry.content_crc32) {
				if (data) heap_caps_free(data);
				epaper_frame_sd_cache_remove(entry.content_crc32);
				epaper_frame_offline_queue_invalidate();
				epaper_frame_refresh_finish_service(&out, started);
				LOGW("Epaper", "Offline queue head cache validation failed");
				return out;
		}

		EpaperNextPayload payload = {};
		payload.result = EpaperNextResult::Show;
		payload.data = data;
		payload.len = length;
		payload.content_crc32 = entry.content_crc32;
		payload.from_cache = true;
		strlcpy(payload.image_key, entry.image_key,
				sizeof(payload.image_key));
		strlcpy(payload.media_type, entry.media_type,
				sizeof(payload.media_type));
		if (!epaper_frame_driver_prepare_service_blob(
				payload.data, payload.len, payload.media_type,
				&payload.prepared_data, &payload.prepared_len)) {
				epaper_frame_next_payload_release(&payload);
				epaper_frame_sd_cache_remove(entry.content_crc32);
				epaper_frame_offline_queue_invalidate();
				out.result = EpaperRefreshResult::FailedDraw;
				epaper_frame_refresh_finish_service(&out, started);
				LOGW("Epaper", "Offline queue head media validation failed");
				return out;
		}
		epaper_frame_timing_set_fetch_source(
				millis() - cache_started, EpaperImageSource::OfflineQueue);
		out.crc_used = entry.content_crc32;
		const bool drew = epaper_frame_refresh_draw_service_payload(
				&payload, &out, started);
		epaper_frame_next_payload_release(&payload);
		if (drew) {
				epaper_frame_offline_queue_complete(true);
				out.result = EpaperRefreshResult::Updated;
		} else {
				epaper_frame_offline_queue_invalidate();
				out.result = EpaperRefreshResult::FailedDraw;
		}
		epaper_frame_refresh_finish_service(&out, started);
		return out;
}
#endif

EpaperRefreshOutcome epaper_frame_refresh_run(DeviceConfig* config, bool force,
		uint32_t fetch_timeout_ms, uint32_t overall_timeout_ms) {
		if (epaper_frame_source_uses_service(g_epaper_config.source_mode)) {
#if defined(BOARD_RETERMINAL_E1003_FRAME)
				if (force) epaper_frame_offline_queue_invalidate();
				return epaper_frame_refresh_run_service(config, fetch_timeout_ms,
						overall_timeout_ms);
#else
				EpaperRefreshOutcome unsupported = {
						EpaperRefreshResult::Disabled, 0, 0, 0, 0, 0};
				return unsupported;
#endif
		}
		return epaper_frame_refresh_run_url(config, g_epaper_config.epaper_frame_url,
				force, true /*allow_crc*/, true /*persist_crc*/);
}

EpaperRefreshOutcome epaper_frame_refresh_show_url(DeviceConfig* config, const char* image_url) {
#if defined(BOARD_RETERMINAL_E1003_FRAME)
		epaper_frame_offline_queue_invalidate();
#endif
		return epaper_frame_refresh_run_url(config, image_url,
				true /*force*/, false /*allow_crc*/, false /*persist_crc*/);
}

uint32_t epaper_frame_refresh_last_unix() {
		return g_last_refresh_unix;
}

EpaperRefreshOutcome epaper_frame_refresh_last_outcome() {
		return s_last_outcome;
}

uint32_t epaper_frame_refresh_get_count() {
		return g_refresh_count;
}

#endif // IS_EPAPER_FRAME
