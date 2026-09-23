#include "epaper_frame_offline_queue_logic.h"

#include <limits.h>
#include <string.h>

namespace {

uint32_t fnv1a_bytes(uint32_t hash, const void* data, size_t length) {
		const uint8_t* bytes = static_cast<const uint8_t*>(data);
		for (size_t index = 0; index < length; ++index) {
				hash ^= bytes[index];
				hash *= 16777619U;
		}
		return hash;
}

uint32_t fnv1a_string(uint32_t hash, const char* value) {
		const char* text = value ? value : "";
		hash = fnv1a_bytes(hash, text, strlen(text));
		const uint8_t separator = 0xff;
		return fnv1a_bytes(hash, &separator, sizeof(separator));
}

bool valid_image_key(const char* value) {
		if (!value) return false;
		const size_t length = strnlen(value, 65);
		if (length == 0 || length >= 65) return false;
		for (size_t index = 0; index < length; ++index) {
				const char ch = value[index];
				const bool valid = (ch >= 'a' && ch <= 'z') ||
						(ch >= 'A' && ch <= 'Z') ||
						(ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
				if (!valid) return false;
		}
		return true;
}

bool valid_media_type(const char* value) {
		return value && (
				strcmp(value, "image/jpeg") == 0 ||
				strcmp(value, "application/vnd.photoframe.g16p") == 0 ||
				strcmp(value, "application/vnd.photoframe.g16z") == 0);
}

bool valid_entry(const EpaperOfflineQueueEntry& entry) {
		return valid_image_key(entry.image_key) &&
				valid_media_type(entry.media_type) &&
				entry.content_length > 0;
}

} // namespace

bool epaper_frame_offline_refresh_count_valid(uint32_t value) {
		return value <= EPAPER_FRAME_OFFLINE_QUEUE_CAPACITY;
}

bool epaper_frame_offline_batch_count(uint32_t offline_refreshes, uint8_t* total_count) {
		if (!total_count ||
				!epaper_frame_offline_refresh_count_valid(offline_refreshes)) {
				return false;
		}
		*total_count = static_cast<uint8_t>(offline_refreshes + 1U);
		return *total_count >= 1 && *total_count <= EPAPER_FRAME_BATCH_MAX_COUNT;
}

uint32_t epaper_frame_offline_config_identity(uint8_t source_mode,
		const char* service_url, const char* service_token,
		bool sd_cache_enabled, uint32_t offline_refreshes) {
		uint32_t hash = 2166136261U;
		hash = fnv1a_bytes(hash, &source_mode, sizeof(source_mode));
		hash = fnv1a_string(hash, service_url);
		hash = fnv1a_string(hash, service_token);
		const uint8_t cache = sd_cache_enabled ? 1 : 0;
		hash = fnv1a_bytes(hash, &cache, sizeof(cache));
		hash = fnv1a_bytes(hash, &offline_refreshes, sizeof(offline_refreshes));
		return hash == 0 ? 1 : hash;
}

EpaperOfflineWakeDecision epaper_frame_offline_wake_decision(
		const EpaperOfflineWakeInputs& inputs) {
		const bool timer_wake = inputs.deep_sleep_wake && !inputs.button_wake;
		if (timer_wake && !inputs.schedule_allowed) {
				return EpaperOfflineWakeDecision::ScheduleSuppressed;
		}
		const bool prerequisites = inputs.supported && inputs.service_mode &&
				inputs.sd_cache_enabled && inputs.offline_refreshes > 0 &&
				epaper_frame_offline_refresh_count_valid(inputs.offline_refreshes);
		return timer_wake && prerequisites && inputs.queue_valid
				? EpaperOfflineWakeDecision::Offline
				: EpaperOfflineWakeDecision::Online;
}

void epaper_frame_offline_queue_reset(EpaperOfflineQueueState* state,
		uint32_t config_identity) {
		if (!state) return;
		memset(state, 0, sizeof(*state));
		state->magic = EPAPER_FRAME_OFFLINE_QUEUE_MAGIC;
		state->version = EPAPER_FRAME_OFFLINE_QUEUE_VERSION;
		state->config_identity = config_identity;
}

bool epaper_frame_offline_queue_state_valid(const EpaperOfflineQueueState& state,
		uint32_t config_identity) {
		if (state.magic != EPAPER_FRAME_OFFLINE_QUEUE_MAGIC ||
				state.version != EPAPER_FRAME_OFFLINE_QUEUE_VERSION ||
				state.config_identity != config_identity ||
				state.head >= EPAPER_FRAME_OFFLINE_QUEUE_CAPACITY ||
				state.count > EPAPER_FRAME_OFFLINE_QUEUE_CAPACITY) {
				return false;
		}
		for (uint8_t offset = 0; offset < state.count; ++offset) {
				const uint8_t index = static_cast<uint8_t>(
						(state.head + offset) % EPAPER_FRAME_OFFLINE_QUEUE_CAPACITY);
				if (!valid_entry(state.entries[index])) return false;
		}
		return true;
}

bool epaper_frame_offline_queue_append(EpaperOfflineQueueState* state,
		const EpaperOfflineQueueEntry& entry) {
		if (!state || !valid_entry(entry) ||
				state->magic != EPAPER_FRAME_OFFLINE_QUEUE_MAGIC ||
				state->version != EPAPER_FRAME_OFFLINE_QUEUE_VERSION ||
				state->count >= EPAPER_FRAME_OFFLINE_QUEUE_CAPACITY ||
				state->head >= EPAPER_FRAME_OFFLINE_QUEUE_CAPACITY) {
				return false;
		}
		const uint8_t index = static_cast<uint8_t>(
				(state->head + state->count) % EPAPER_FRAME_OFFLINE_QUEUE_CAPACITY);
		state->entries[index] = entry;
		++state->count;
		return true;
}

const EpaperOfflineQueueEntry* epaper_frame_offline_queue_head(
		const EpaperOfflineQueueState& state) {
		if (state.count == 0 || state.head >= EPAPER_FRAME_OFFLINE_QUEUE_CAPACITY) {
				return nullptr;
		}
		return &state.entries[state.head];
}

bool epaper_frame_offline_queue_complete_head(EpaperOfflineQueueState* state,
		bool display_succeeded) {
		if (!state || state->count == 0 ||
				state->head >= EPAPER_FRAME_OFFLINE_QUEUE_CAPACITY) {
				return false;
		}
		if (!display_succeeded) return false;
		memset(&state->entries[state->head], 0, sizeof(state->entries[state->head]));
		state->head = static_cast<uint8_t>(
				(state->head + 1) % EPAPER_FRAME_OFFLINE_QUEUE_CAPACITY);
		--state->count;
		return true;
}

void epaper_frame_offline_telemetry_record(EpaperOfflineTelemetryAggregate* aggregate,
		uint8_t result, uint32_t elapsed_ms, uint16_t battery_mv,
		uint32_t content_crc32, const char* image_key) {
		if (!aggregate) return;
		if (aggregate->count < UINT32_MAX) ++aggregate->count;
		aggregate->latest_result = result;
		aggregate->latest_elapsed_ms = elapsed_ms;
		aggregate->latest_battery_mv = battery_mv;
		aggregate->latest_content_crc32 = content_crc32;
		strncpy(aggregate->latest_image_key, image_key ? image_key : "",
				sizeof(aggregate->latest_image_key) - 1);
		aggregate->latest_image_key[
				sizeof(aggregate->latest_image_key) - 1] = '\0';
}

void epaper_frame_offline_telemetry_publish_complete(
		EpaperOfflineTelemetryAggregate* aggregate, bool publish_succeeded) {
		if (aggregate && publish_succeeded) {
				memset(aggregate, 0, sizeof(*aggregate));
	}
}
