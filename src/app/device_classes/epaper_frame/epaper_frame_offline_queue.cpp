#include "board_config.h"

#if IS_EPAPER_FRAME && defined(BOARD_RETERMINAL_E1003_FRAME)

#include "device_classes/epaper_frame/epaper_frame_offline_queue.h"

#include "device_classes/epaper_frame/epaper_frame_config.h"
#include "log_manager.h"

#include <esp_attr.h>
#include <string.h>

RTC_DATA_ATTR static EpaperOfflineQueueState s_epaper_offline_queue = {};

uint32_t epaper_frame_offline_queue_current_identity() {
		return epaper_frame_offline_config_identity(
				static_cast<uint8_t>(g_epaper_config.source_mode),
				g_epaper_config.service_url,
				g_epaper_config.service_token,
				g_epaper_config.epaper_frame_sd_cache_enabled,
				g_epaper_config.offline_refreshes_between_syncs);
}

bool epaper_frame_offline_queue_has_valid_entries() {
		const uint32_t identity = epaper_frame_offline_queue_current_identity();
		const bool valid = epaper_frame_offline_queue_state_valid(
				s_epaper_offline_queue, identity);
		if (!valid) {
				if (s_epaper_offline_queue.magic == EPAPER_FRAME_OFFLINE_QUEUE_MAGIC) {
						LOGI("Epaper", "Offline queue stale or invalid; discarding");
				}
				epaper_frame_offline_queue_invalidate();
				return false;
		}
		return s_epaper_offline_queue.count > 0;
}

void epaper_frame_offline_queue_invalidate() {
		memset(&s_epaper_offline_queue, 0, sizeof(s_epaper_offline_queue));
}

void epaper_frame_offline_queue_begin_sync() {
		epaper_frame_offline_queue_reset(&s_epaper_offline_queue,
				epaper_frame_offline_queue_current_identity());
}

bool epaper_frame_offline_queue_append_validated(
		const EpaperOfflineQueueEntry& entry) {
		return epaper_frame_offline_queue_append(&s_epaper_offline_queue, entry);
}

const EpaperOfflineQueueEntry* epaper_frame_offline_queue_peek() {
		if (!epaper_frame_offline_queue_has_valid_entries()) return nullptr;
		return epaper_frame_offline_queue_head(s_epaper_offline_queue);
}

bool epaper_frame_offline_queue_complete(bool display_succeeded) {
		return epaper_frame_offline_queue_complete_head(
				&s_epaper_offline_queue, display_succeeded);
}

uint8_t epaper_frame_offline_queue_count() {
		return epaper_frame_offline_queue_has_valid_entries()
				? s_epaper_offline_queue.count : 0;
}

#endif
