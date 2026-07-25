#pragma once

#include "epaper_ble_codec.h"
#include "device_classes/epaper/epaper_assignment_logic.h"

#include <stdint.h>

enum class EpaperBleFrameAction : uint8_t {
	HttpFallback,
	AcceptUnchanged,
	RenderCached,
};

struct EpaperBleFrameSelection {
	bool found;
	EpaperBleAssignmentPacket packet;
};

bool epaper_ble_frame_consider(EpaperBleFrameSelection* selection,
		const EpaperBleAssignmentPacket& packet, uint32_t own_device_key);
EpaperBleFrameAction epaper_ble_frame_action(
		const EpaperBleFrameSelection& selection,
		const EpaperAssignmentState& accepted, bool cache_valid);
bool epaper_ble_frame_format_supported(const EpaperBleAssignmentPacket& packet);
