#include "board_config.h"

#if IS_EPAPER_FRAME

#include "device_classes/epaper_frame/epaper_frame_config.h"
#include "device_classes/epaper_frame/epaper_frame_wake_log.h"
#include "log_manager.h"
#include "storage.h"

#include <Arduino.h>
#include <time.h>

namespace {

const char* wake_reason_name(EpaperWakeReason reason) {
		switch (reason) {
			case EpaperWakeReason::Timer: return "timer";
			case EpaperWakeReason::Button: return "button";
			case EpaperWakeReason::ColdBoot: return "cold_boot";
		}
		return "unknown";
}

const char* wake_result_name(EpaperWakeResult result) {
		switch (result) {
			case EpaperWakeResult::Updated: return "updated";
			case EpaperWakeResult::Skipped: return "skipped";
			case EpaperWakeResult::FailedFetch: return "fetch_failed";
			case EpaperWakeResult::FailedDraw: return "draw_failed";
			case EpaperWakeResult::WifiFailed: return "wifi_failed";
			case EpaperWakeResult::MqttConnectFailed: return "mqtt_connect_failed";
			case EpaperWakeResult::MqttPublishUnconfirmed: return "mqtt_publish_unconfirmed";
			case EpaperWakeResult::LowBattery: return "low_battery";
			case EpaperWakeResult::ScheduleSuppressed: return "schedule_suppressed";
			case EpaperWakeResult::SourceUnconfigured: return "source_unconfigured";
			case EpaperWakeResult::BudgetExceeded: return "budget_exceeded";
			case EpaperWakeResult::Interrupted: return "interrupted";
			case EpaperWakeResult::InProgress: return "in_progress";
		}
		return "unknown";
}

const char* batch_manifest_status_name(EpaperBatchManifestStatus status) {
		switch (status) {
				case EpaperBatchManifestStatus::NotAttempted: return "not_attempted";
				case EpaperBatchManifestStatus::Show: return "show";
				case EpaperBatchManifestStatus::Keep: return "keep";
				case EpaperBatchManifestStatus::AuthFailed: return "auth_failed";
				case EpaperBatchManifestStatus::UnsupportedMajor: return "unsupported_major";
				case EpaperBatchManifestStatus::FailedFetch: return "fetch_failed";
				case EpaperBatchManifestStatus::FailedContent: return "content_failed";
		}
		return "unknown";
}

} // namespace

void epaper_frame_wake_log_append(const EpaperWakeRecord& record) {
		if (!g_epaper_config.epaper_frame_wake_log_enabled) return;
		const uint32_t started_ms = millis();
		const time_t now = time(nullptr);
		char recorded_at_unix[16] = {};
		if (now >= (time_t)kEpaperMinValidEpoch) {
				snprintf(recorded_at_unix, sizeof(recorded_at_unix), "%lu",
						(unsigned long)now);
		}
		char sidecar_http_status[8] = {};
		if (!epaper_frame_source_uses_service(g_epaper_config.source_mode)) {
				snprintf(sidecar_http_status, sizeof(sidecar_http_status), "%d",
						(int)record.sidecar_http_status);
		}
		if (!storage_mount()) {
			LOGW("EpaperLog", "Wake log unavailable: storage mount failed");
			return;
		}

		File file = Storage.open(EPAPER_FRAME_WAKE_LOG_PATH, "a");
		if (!file) {
			LOGW("EpaperLog", "Wake log unavailable: open failed");
			return;
		}
		if (file.size() == 0) {
						file.println("session_id,wake_id,recorded_at_unix,wake_reason,result,battery_mv,sidecar_http_status,pre_delivery_ms,boot_to_wifi_ms,resolve_ms,selected_image_fetch_ms,draw_ms,ntp_sync_ms,batch_manifest_result,batch_manifest_count,batch_cache_hits,batch_cache_misses,batch_download_failures,batch_download_bytes,batch_http_ms,batch_slowest_request_ms,batch_manifest_http_ms,batch_manifest_http_code,fallback_used,fallback_elapsed_ms,fallback_http_code,previous_sleep_requested,previous_requested_sleep_s");
		}
		const EpaperTimingBudget& timing = record.timing;
		const size_t written = file.printf(
						"%llu,%lu,%s,%s,%s,%u,%s,%lu,%lu,%lu,%lu,%lu,%lu,%s,%u,%u,%u,%u,%lu,%lu,%lu,%lu,%d,%u,%lu,%d,%u,%lu\n",
				(unsigned long long)timing.session_id,
				(unsigned long)timing.wake_id, recorded_at_unix,
				wake_reason_name(record.wake_reason), wake_result_name(record.result),
				(unsigned)record.battery_mv, sidecar_http_status,
				(unsigned long)timing.total_active_ms, (unsigned long)timing.boot_to_wifi_ms,
				(unsigned long)timing.resolve_ms, (unsigned long)timing.fetch_ms,
				(unsigned long)timing.draw_ms, (unsigned long)timing.ntp_sync_ms,
				batch_manifest_status_name(timing.batch_manifest_status),
				(unsigned)timing.batch_manifest_count, (unsigned)timing.batch_cache_hits,
				(unsigned)timing.batch_cache_misses,
				(unsigned)timing.batch_download_failures,
				(unsigned long)timing.batch_download_bytes,
				(unsigned long)timing.batch_http_ms,
				(unsigned long)timing.batch_slowest_request_ms,
						(unsigned long)timing.batch_manifest_http_ms,
						(int)timing.batch_manifest_http_code,
						(unsigned)timing.batch_fallback_used,
						(unsigned long)timing.fallback_elapsed_ms,
						(int)timing.fallback_http_code,
						(unsigned)timing.previous_sleep_requested,
						(unsigned long)timing.previous_requested_sleep_s);
		file.close();
		if (written == 0) {
			LOGW("EpaperLog", "Wake log append failed after %lums", (unsigned long)(millis() - started_ms));
			return;
		}
		LOGI("EpaperLog", "Wake log appended in %lums", (unsigned long)(millis() - started_ms));
}

bool epaper_frame_wake_log_clear() {
		if (!storage_mount()) return false;
		return !Storage.exists(EPAPER_FRAME_WAKE_LOG_PATH) ||
				Storage.remove(EPAPER_FRAME_WAKE_LOG_PATH);
}

#endif // IS_EPAPER_FRAME