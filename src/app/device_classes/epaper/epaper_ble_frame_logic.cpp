#include "device_classes/epaper/epaper_ble_frame_logic.h"

#include <string.h>

bool epaper_ble_frame_consider(EpaperBleFrameSelection* selection,
		const EpaperBleAssignmentPacket& packet, uint32_t own_device_key) {
	if (!selection || packet.device_key != own_device_key) return false;
	if (!selection->found ||
			epaper_ble_revision_compare(packet.revision,
				selection->packet.revision) > 0) {
		selection->found = true;
		selection->packet = packet;
		return true;
	}
	return false;
}

bool epaper_ble_frame_format_supported(const EpaperBleAssignmentPacket& packet) {
	return packet.reserved_flags == 0 &&
		(packet.image_format == EPAPER_BLE_FORMAT_G16Z ||
		 packet.image_format == EPAPER_BLE_FORMAT_JPEG);
}

EpaperBleFrameAction epaper_ble_frame_action(
		const EpaperBleFrameSelection& selection,
		const EpaperAssignmentState& accepted, bool cache_valid) {
	if (!selection.found) return EpaperBleFrameAction::HttpFallback;
	const EpaperBleAssignmentPacket& packet = selection.packet;
	if (!packet.valid || packet.revision == 0 ||
			!epaper_ble_frame_format_supported(packet)) {
		return EpaperBleFrameAction::HttpFallback;
	}
	if (accepted.revision != 0) {
		const int revision_order = epaper_ble_revision_compare(
			packet.revision, accepted.revision);
		if (revision_order < 0) return EpaperBleFrameAction::HttpFallback;
		const bool same_content =
			memcmp(packet.image_key, accepted.image_key,
				sizeof(packet.image_key)) == 0 &&
			packet.content_crc32 != 0 &&
			packet.content_crc32 == accepted.content_crc32;
		if (same_content) return EpaperBleFrameAction::AcceptUnchanged;
		if (revision_order == 0) return EpaperBleFrameAction::HttpFallback;
	}
	return cache_valid ? EpaperBleFrameAction::RenderCached
		: EpaperBleFrameAction::HttpFallback;
}
