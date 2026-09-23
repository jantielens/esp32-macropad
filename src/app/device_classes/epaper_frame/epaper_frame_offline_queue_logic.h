#pragma once
#ifndef EPAPER_FRAME_OFFLINE_QUEUE_LOGIC_H
#define EPAPER_FRAME_OFFLINE_QUEUE_LOGIC_H

#include <stddef.h>
#include <stdint.h>

constexpr uint8_t EPAPER_FRAME_OFFLINE_QUEUE_CAPACITY = 16;
constexpr uint8_t EPAPER_FRAME_BATCH_MAX_COUNT =
		EPAPER_FRAME_OFFLINE_QUEUE_CAPACITY + 1;
constexpr size_t EPAPER_FRAME_OFFLINE_IMAGE_KEY_SIZE = 65;
constexpr uint32_t EPAPER_FRAME_OFFLINE_QUEUE_MAGIC = 0x45504f51U;
constexpr uint8_t EPAPER_FRAME_OFFLINE_QUEUE_VERSION = 1;

struct EpaperOfflineQueueEntry {
		char image_key[EPAPER_FRAME_OFFLINE_IMAGE_KEY_SIZE];
		char media_type[48];
		uint32_t content_crc32;
		uint32_t content_length;
};

struct EpaperOfflineQueueState {
		uint32_t magic;
		uint32_t config_identity;
		uint8_t version;
		uint8_t head;
		uint8_t count;
		EpaperOfflineQueueEntry entries[EPAPER_FRAME_OFFLINE_QUEUE_CAPACITY];
};

enum class EpaperOfflineWakeDecision : uint8_t {
		Online,
		Offline,
		ScheduleSuppressed,
};

struct EpaperOfflineWakeInputs {
		bool supported;
		bool service_mode;
		bool sd_cache_enabled;
		uint32_t offline_refreshes;
		bool deep_sleep_wake;
		bool button_wake;
		bool schedule_allowed;
		bool queue_valid;
};

struct EpaperOfflineTelemetryAggregate {
		uint32_t count;
		uint32_t latest_elapsed_ms;
		uint32_t latest_content_crc32;
		uint16_t latest_battery_mv;
		uint8_t latest_result;
		char latest_image_key[EPAPER_FRAME_OFFLINE_IMAGE_KEY_SIZE];
};

bool epaper_frame_offline_refresh_count_valid(uint32_t value);
bool epaper_frame_offline_batch_count(uint32_t offline_refreshes, uint8_t* total_count);
uint32_t epaper_frame_offline_config_identity(uint8_t source_mode,
		const char* service_url, const char* service_token,
		bool sd_cache_enabled, uint32_t offline_refreshes);
EpaperOfflineWakeDecision epaper_frame_offline_wake_decision(
		const EpaperOfflineWakeInputs& inputs);

void epaper_frame_offline_queue_reset(EpaperOfflineQueueState* state,
		uint32_t config_identity);
bool epaper_frame_offline_queue_state_valid(const EpaperOfflineQueueState& state,
		uint32_t config_identity);
bool epaper_frame_offline_queue_append(EpaperOfflineQueueState* state,
		const EpaperOfflineQueueEntry& entry);
const EpaperOfflineQueueEntry* epaper_frame_offline_queue_head(
		const EpaperOfflineQueueState& state);
bool epaper_frame_offline_queue_complete_head(EpaperOfflineQueueState* state,
		bool display_succeeded);

void epaper_frame_offline_telemetry_record(EpaperOfflineTelemetryAggregate* aggregate,
		uint8_t result, uint32_t elapsed_ms, uint16_t battery_mv,
		uint32_t content_crc32, const char* image_key);
void epaper_frame_offline_telemetry_publish_complete(
		EpaperOfflineTelemetryAggregate* aggregate, bool publish_succeeded);

#endif
