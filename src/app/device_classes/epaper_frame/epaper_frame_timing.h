#pragma once
#ifndef EPAPER_FRAME_TIMING_H
#define EPAPER_FRAME_TIMING_H

#include "board_config.h"

#if IS_EPAPER_FRAME

#include <stdint.h>

// Per-wake timing + diagnostic snapshot. Populated by the e-paper duty cycle,
// retained across deep sleep in RTC memory so the portal can show "last cycle"
// numbers and MQTT can publish them on the next wake.
//
// Cleared on cold boot (power loss / USB unplug) — portal shows zero values
// until the first full cycle completes.
struct EpaperTimingBudget {
		uint64_t session_id;
		uint32_t wake_id;
		uint32_t boot_to_wifi_ms;   // power-on -> WiFi connected
		int16_t  wifi_rssi;         // captured immediately after WiFi connect
		uint8_t  crc_retry_count;   // attempts made by sidecar fetcher (1 = no retries)
		uint32_t ntp_sync_ms;       // time spent waiting for the per-wake NTP resync
		uint32_t crc_to_draw_ms;    // WiFi connected -> drawImage + display done (includes sidecar fetch)
		uint32_t draw_to_mqtt_ms;   // display done -> MQTT publish done
		uint32_t total_active_ms;   // power-on -> just before deep sleep

		// Image-fetch/render breakdown (a subset of crc_to_draw_ms), staged by the
		// active board driver as each phase of epaper_frame_driver_draw_url()/display()
		// completes. Zero on boards or paths that don't decompose the draw (e.g. a
		// CRC-skip wake, where no fetch/draw happens).
		uint32_t resolve_ms;        // URL/API redirect round-trip (0 if no resolve)
		uint32_t fetch_ms;          // image bytes: SD cache read OR HTTP download
		uint32_t draw_ms;           // framebuffer upload + panel GC16 refresh
		uint8_t  image_from_cache;  // EpaperImageSource encoded below
		uint32_t overall_budget_ms; // 0 = budget not enforced for this wake
		uint32_t budget_elapsed_ms; // elapsed when the wake record was finalized
		uint32_t budget_remaining_ms;
		uint32_t wifi_limit_ms;
		uint32_t fetch_limit_ms;
		uint32_t mqtt_limit_ms;
		uint32_t wifi_target_ms;
		uint32_t fetch_target_ms;
		uint32_t mqtt_target_ms;
		uint8_t budget_cut;         // EpaperWakeBudgetCut encoded as uint8_t
};

enum class EpaperImageSource : uint8_t {
		Download = 0,
		OnlineCache = 1,
		OfflineQueue = 2,
};

extern EpaperTimingBudget epaper_frame_timing_last;

enum class EpaperWakeReason : uint8_t {
		Timer,
		Button,
		ColdBoot,
};

enum class EpaperWakeStage : uint8_t {
		Boot,
		Battery,
		Wifi,
		Refresh,
		Ntp,
		MqttConnect,
		MqttPublish,
};

enum class EpaperWakeResult : uint8_t {
		InProgress,
		Updated,
		Skipped,
		FailedFetch,
		FailedDraw,
		WifiFailed,
		MqttConnectFailed,
		MqttPublishUnconfirmed,
		LowBattery,
		ScheduleSuppressed,
		SourceUnconfigured,
		BudgetExceeded,
		Interrupted,
};

struct EpaperPreviousDelivery {
		uint64_t session_id;
		uint32_t wake_id;
		uint32_t mqtt_connect_ms;
		uint32_t mqtt_publish_ms;
		uint32_t total_active_ms;
		bool valid;
};

struct EpaperWakeRecord {
		EpaperTimingBudget timing;
		EpaperPreviousDelivery previous_delivery;
		uint16_t battery_mv;
		int16_t sidecar_http_status;
		EpaperWakeReason wake_reason;
		EpaperWakeStage last_stage;
		EpaperWakeResult result;
		EpaperWakeResult refresh_result;
};

constexpr uint8_t EPAPER_FRAME_WAKE_JOURNAL_CAPACITY = 16;

uint32_t epaper_frame_timing_begin_wake(EpaperWakeReason wake_reason);
uint64_t epaper_frame_wake_journal_session_id();
void epaper_frame_wake_journal_checkpoint(EpaperWakeStage stage);
void epaper_frame_wake_journal_finalize(EpaperWakeResult result, uint16_t battery_mv,
															 int16_t sidecar_http_status);
void epaper_frame_wake_journal_mark_delivery_failed(EpaperWakeResult result);
void epaper_frame_wake_journal_complete_delivery(uint32_t mqtt_connect_ms,
																	 uint32_t mqtt_publish_ms, uint32_t total_active_ms);
bool epaper_frame_wake_journal_peek(EpaperWakeRecord* record);
void epaper_frame_wake_journal_remove_oldest();
uint32_t epaper_frame_wake_journal_dropped_count();
void epaper_frame_wake_journal_clear_dropped_count();

// Sub-step timing setters called by the active e-paper driver as each phase
// completes. They write directly into epaper_frame_timing_last. The duty cycle calls
// epaper_frame_timing_reset_draw_steps() before each refresh so a CRC-skip wake
// reports zeros instead of the previous cycle's values.
void epaper_frame_timing_reset_draw_steps();
void epaper_frame_timing_set_resolve_ms(uint32_t ms);
void epaper_frame_timing_set_fetch(uint32_t ms, bool from_cache);
void epaper_frame_timing_set_fetch_source(uint32_t ms, EpaperImageSource source);
void epaper_frame_timing_set_draw_ms(uint32_t ms);

#endif // IS_EPAPER_FRAME

#endif // EPAPER_FRAME_TIMING_H
