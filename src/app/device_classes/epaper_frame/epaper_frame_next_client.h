#pragma once

#ifndef EPAPER_FRAME_NEXT_CLIENT_H
#define EPAPER_FRAME_NEXT_CLIENT_H

#include "board_config.h"

#if IS_EPAPER_FRAME && defined(BOARD_RETERMINAL_E1003_FRAME)

#include "device_classes/epaper_frame/epaper_frame_next_client_logic.h"
#include "device_classes/epaper_frame/epaper_frame_offline_queue_logic.h"

#include <stddef.h>
#include <stdint.h>

struct EpaperCurrentFingerprint {
		bool valid;
		char image_key[65];
		uint32_t content_crc32;
};

struct EpaperNextPayload {
		EpaperNextResult result;
		uint8_t* data;
		size_t len;
		uint8_t* prepared_data;
		size_t prepared_len;
		char image_key[65];
		char media_type[48];
		uint32_t content_crc32;
		bool from_cache;
		size_t body_bytes_read;
};

constexpr size_t EPAPER_FRAME_CONTENT_REF_MAX_LEN = 384;

struct EpaperBatchEntry {
		char image_key[65];
		char media_type[48];
		char content_ref[EPAPER_FRAME_CONTENT_REF_MAX_LEN];
		uint32_t content_crc32;
		uint32_t content_length;
};

struct EpaperBatchManifest {
		uint8_t count;
		EpaperBatchEntry entries[EPAPER_FRAME_BATCH_MAX_COUNT];
};

EpaperNextPayload epaper_frame_next_client_fetch(const char* service_base,
		const char* bearer_token, const EpaperCurrentFingerprint& current,
		bool cache_enabled, uint8_t max_cycles = 2, uint32_t timeout_ms = 0);
EpaperNextResult epaper_frame_next_client_fetch_batch_manifest(
		const char* service_base, const char* bearer_token,
		const EpaperCurrentFingerprint& current, uint8_t requested_count,
		EpaperBatchManifest** manifest, uint32_t timeout_ms = 0);
EpaperNextPayload epaper_frame_next_client_fetch_batch_entry(
		const char* service_base, const char* bearer_token,
		const EpaperBatchEntry& entry, bool cache_enabled,
		uint32_t timeout_ms = 0);
void epaper_frame_batch_manifest_release(EpaperBatchManifest* manifest);
void epaper_frame_next_payload_release(EpaperNextPayload* payload);

#endif

#endif // EPAPER_FRAME_NEXT_CLIENT_H
