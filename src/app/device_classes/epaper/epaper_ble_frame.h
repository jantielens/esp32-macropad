#pragma once

#include "board_config.h"

#if HAS_EPAPER && HAS_BLE

#include "device_classes/epaper/epaper_ble_frame_logic.h"

struct DeviceConfig;

enum class EpaperBleFrameWakeResult : uint8_t {
	HttpFallback,
	AcceptedUnchanged,
	RenderedCached,
};

EpaperBleFrameWakeResult epaper_ble_frame_try(DeviceConfig* config,
		EpaperBleFrameSelection* selection_out);
void epaper_ble_frame_ack(const EpaperBleAssignmentPacket& packet);

#endif
