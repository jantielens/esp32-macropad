#include "device_classes/epaper/epaper_ble_frame_logic.h"

#include <assert.h>
#include <string.h>

namespace {

EpaperBleAssignmentPacket packet(uint32_t revision, bool valid = true,
		uint8_t format = EPAPER_BLE_FORMAT_G16Z) {
	EpaperBleAssignmentPacket value = {};
	value.valid = valid;
	value.image_format = format;
	value.device_key = 0x12345678;
	value.revision = revision;
	value.content_crc32 = 0xAABBCCDD;
	memset(value.image_key, 0x42, sizeof(value.image_key));
	return value;
}

} // namespace

int main() {
	EpaperAssignmentState accepted = {};
	accepted.revision = 9;
	accepted.content_crc32 = 0xAABBCCDD;
	memset(accepted.image_key, 0x42, sizeof(accepted.image_key));

	EpaperBleFrameSelection selection = {};
	assert(!epaper_ble_frame_consider(&selection, packet(99), 0xDEADBEEF));
	assert(epaper_ble_frame_consider(&selection, packet(10), 0x12345678));
	assert(!epaper_ble_frame_consider(&selection, packet(10), 0x12345678));
	assert(!epaper_ble_frame_consider(&selection, packet(8), 0x12345678));
	assert(epaper_ble_frame_consider(&selection, packet(11, false), 0x12345678));
	assert(selection.packet.revision == 11 && !selection.packet.valid);
	assert(epaper_ble_frame_action(selection, accepted, true) ==
		EpaperBleFrameAction::HttpFallback);

	selection = {};
	epaper_ble_frame_consider(&selection, packet(10), 0x12345678);
	assert(epaper_ble_frame_action(selection, accepted, false) ==
		EpaperBleFrameAction::AcceptUnchanged);

	selection.packet.content_crc32 ^= 1;
	assert(epaper_ble_frame_action(selection, accepted, false) ==
		EpaperBleFrameAction::HttpFallback);
	assert(epaper_ble_frame_action(selection, accepted, true) ==
		EpaperBleFrameAction::RenderCached);

	selection.packet.revision = 8;
	assert(epaper_ble_frame_action(selection, accepted, true) ==
		EpaperBleFrameAction::HttpFallback);
	selection.packet.revision = 10;
	selection.packet.image_format = EPAPER_BLE_FORMAT_G16P;
	assert(epaper_ble_frame_action(selection, accepted, true) ==
		EpaperBleFrameAction::HttpFallback);
	selection.packet.image_format = EPAPER_BLE_FORMAT_G16Z;
	selection.packet.reserved_flags = 1;
	assert(epaper_ble_frame_action(selection, accepted, true) ==
		EpaperBleFrameAction::HttpFallback);

	EpaperBleFrameSelection wrapped = {};
	epaper_ble_frame_consider(&wrapped, packet(0xFFFFFFF0), 0x12345678);
	assert(epaper_ble_frame_consider(&wrapped, packet(5), 0x12345678));
	assert(wrapped.packet.revision == 5);

	char device_id[32] = {};
	char api_key[32] = {};
	assert(epaper_assignment_extract_credentials(
		"https://example/api/next?device_id=frame%201&key=a%2Bb",
		device_id, sizeof(device_id), api_key, sizeof(api_key)));
	assert(strcmp(device_id, "frame 1") == 0);
	assert(strcmp(api_key, "a+b") == 0);
	assert(!epaper_assignment_extract_credentials(
		"https://example/api/next?device_id=frame", device_id,
		sizeof(device_id), api_key, sizeof(api_key)));
	return 0;
}