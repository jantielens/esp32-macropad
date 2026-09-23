#pragma once

#include "board_config.h"

#if IS_EPAPER_FRAME && defined(BOARD_RETERMINAL_E1003_FRAME)

#include "epaper_frame_offline_queue_logic.h"

uint32_t epaper_frame_offline_queue_current_identity();
bool epaper_frame_offline_queue_has_valid_entries();
void epaper_frame_offline_queue_invalidate();
void epaper_frame_offline_queue_begin_sync();
bool epaper_frame_offline_queue_append_validated(
		const EpaperOfflineQueueEntry& entry);
const EpaperOfflineQueueEntry* epaper_frame_offline_queue_peek();
bool epaper_frame_offline_queue_complete(bool display_succeeded);
uint8_t epaper_frame_offline_queue_count();

#endif
