#if HAS_EPAPER && HAS_BLE

#include <Arduino.h>
#include <BLEAdvertising.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <string.h>

#undef LOG_LEVEL_ERROR
#undef LOG_LEVEL_WARN
#undef LOG_LEVEL_INFO
#undef LOG_LEVEL_DEBUG

namespace {

constexpr uint32_t kScanDeadlineMs = 400;
constexpr uint32_t kAckIntervalMs = 30;
constexpr uint32_t kAckAdvertisementCount = 3;

EpaperBleFrameSelection* s_selection = nullptr;
uint32_t s_device_key = 0;
uint32_t s_scan_started_ms = 0;
int16_t s_winner_rssi = 0;

void image_key_hex(const uint8_t key[8], char out[17]) {
	static const char digits[] = "0123456789abcdef";
	for (size_t i = 0; i < 8; ++i) {
		out[i * 2] = digits[key[i] >> 4];
		out[i * 2 + 1] = digits[key[i] & 0x0F];
	}
	out[16] = '\0';
}

EpaperAssignmentState accepted_state(const EpaperBleAssignmentPacket& packet) {
	EpaperAssignmentState state = {};
	state.revision = packet.revision;
	memcpy(state.image_key, packet.image_key, sizeof(state.image_key));
	state.content_crc32 = packet.content_crc32;
	return state;
}

class AssignmentCallbacks final : public BLEAdvertisedDeviceCallbacks {
	void onResult(BLEAdvertisedDevice device) override {
		if (!device.haveManufacturerData() || !s_selection) return;
		const String data = device.getManufacturerData();
		if (data.length() != EPAPER_BLE_MANUFACTURER_DATA_SIZE) return;
		const uint8_t* bytes = (const uint8_t*)data.c_str();
		const uint16_t company_id =
			(uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
		EpaperBleAssignmentPacket packet = {};
		if (epaper_ble_decode_assignment(company_id, bytes + 2,
				EPAPER_BLE_PACKET_SIZE, &packet) != EpaperBleCodecResult::Ok) {
			return;
		}
		++epaper_timing_last.ble_packets_seen;
		if (epaper_ble_frame_consider(s_selection, packet, s_device_key)) {
			s_winner_rssi = (int16_t)device.getRSSI();
			epaper_timing_last.ble_match_ms = millis() - s_scan_started_ms;
		}
	}
};

AssignmentCallbacks s_callbacks;

bool start_ble() {
	const uint32_t started = millis();
	if (!BLEDevice::init("")) {
		LOGW("Epaper", "BLE assignment init failed");
		return false;
	}
	epaper_timing_last.ble_init_ms = millis() - started;
	return BLEDevice::getInitialized();
}

void stop_ble() {
	if (BLEDevice::getInitialized()) BLEDevice::deinit(true);
}

bool scan(EpaperBleFrameSelection* selection) {
	const uint32_t entered = millis();
	LOGI("Epaper", "BLE assignment scan start (%lums deadline)",
		(unsigned long)kScanDeadlineMs);
	if (!start_ble()) return false;
	BLEScan* scanner = BLEDevice::getScan();
	if (!scanner) {
		LOGW("Epaper", "BLE assignment scanner unavailable");
		return false;
	}
	s_selection = selection;
	s_scan_started_ms = entered;
	s_winner_rssi = 0;
	scanner->setActiveScan(false);
	scanner->setInterval(100);
	scanner->setWindow(100);
	scanner->setAdvertisedDeviceCallbacks(&s_callbacks, true);
	if ((uint32_t)(millis() - entered) < kScanDeadlineMs) {
		scanner->start(0, nullptr, false);
	}
	while ((uint32_t)(millis() - entered) < kScanDeadlineMs) delay(5);
	scanner->stop();
	s_selection = nullptr;
	epaper_timing_last.ble_scan_ms = millis() - entered;
	epaper_timing_last.ble_rssi = s_winner_rssi;
	if (selection->found) {
		LOGI("Epaper", "BLE assignment selected: rev=%lu format=%u valid=%u flags=0x%02x rssi=%d packets=%lu scan=%lums",
			(unsigned long)selection->packet.revision,
			(unsigned)selection->packet.image_format,
			selection->packet.valid ? 1u : 0u,
			(unsigned)selection->packet.reserved_flags,
			(int)s_winner_rssi,
			(unsigned long)epaper_timing_last.ble_packets_seen,
			(unsigned long)epaper_timing_last.ble_scan_ms);
	} else {
		LOGI("Epaper", "BLE assignment scan complete: no matching packet (decoded=%lu, %lums); using HTTP fallback",
			(unsigned long)epaper_timing_last.ble_packets_seen,
			(unsigned long)epaper_timing_last.ble_scan_ms);
	}
	return selection->found;
}

void log_http_fallback_reason(const EpaperBleFrameSelection& selection,
		const EpaperAssignmentState& accepted) {
	const EpaperBleAssignmentPacket& packet = selection.packet;
	if (!packet.valid) {
		LOGI("Epaper", "BLE assignment rev=%lu marked invalid; using HTTP fallback",
			(unsigned long)packet.revision);
	} else if (packet.revision == 0) {
		LOGI("Epaper", "BLE assignment has revision zero; using HTTP fallback");
	} else if (packet.reserved_flags != 0) {
		LOGI("Epaper", "BLE assignment rev=%lu has reserved flags 0x%02x; using HTTP fallback",
			(unsigned long)packet.revision, (unsigned)packet.reserved_flags);
	} else if (!epaper_ble_frame_format_supported(packet)) {
		LOGI("Epaper", "BLE assignment rev=%lu has unsupported format %u; using HTTP fallback",
			(unsigned long)packet.revision, (unsigned)packet.image_format);
	} else if (accepted.revision != 0 &&
			epaper_ble_revision_compare(packet.revision, accepted.revision) < 0) {
		LOGI("Epaper", "BLE assignment rev=%lu is older than accepted rev=%lu; using HTTP fallback",
			(unsigned long)packet.revision, (unsigned long)accepted.revision);
	} else {
		LOGI("Epaper", "BLE assignment rev=%lu is not acceptable; using HTTP fallback",
			(unsigned long)packet.revision);
	}
}

} // namespace

void epaper_ble_frame_ack(const EpaperBleAssignmentPacket& packet) {
	char device_id[64] = {};
	char api_key[128] = {};
	if (!epaper_assignment_extract_credentials(
			g_epaper_config.assignment_source_url, device_id, sizeof(device_id),
			api_key, sizeof(api_key))) {
		LOGW("Epaper", "BLE assignment ACK skipped: assignment credentials unavailable");
		return;
	}
	if (!BLEDevice::getInitialized() && !start_ble()) return;
	EpaperBleAckPacket ack = {};
	ack.result = 1;
	ack.device_key = packet.device_key;
	ack.revision = packet.revision;
	memcpy(ack.image_key, packet.image_key, sizeof(ack.image_key));
	uint8_t manufacturer_data[EPAPER_BLE_MANUFACTURER_DATA_SIZE];
	manufacturer_data[0] = (uint8_t)(EPAPER_BLE_COMPANY_ID & 0xFF);
	manufacturer_data[1] = (uint8_t)(EPAPER_BLE_COMPANY_ID >> 8);
	if (!epaper_ble_encode_ack(ack, api_key, manufacturer_data + 2)) {
		LOGW("Epaper", "BLE assignment ACK encoding failed for rev=%lu",
			(unsigned long)packet.revision);
		stop_ble();
		return;
	}
	String data;
	data.concat((const char*)manufacturer_data, sizeof(manufacturer_data));
	BLEAdvertisementData advertisement;
	advertisement.setFlags(0x04);
	advertisement.setManufacturerData(data);
	BLEAdvertising* advertising = BLEDevice::getAdvertising();
	if (advertising) {
		const uint32_t started = millis();
		LOGI("Epaper", "BLE assignment ACK advertise: rev=%lu count=%lu interval=%lums",
			(unsigned long)packet.revision,
			(unsigned long)kAckAdvertisementCount,
			(unsigned long)kAckIntervalMs);
		advertising->setAdvertisementData(advertisement);
		advertising->setAdvertisementType(BLE_GAP_CONN_MODE_NON);
		advertising->setScanResponse(false);
		advertising->setMinInterval(48);
		advertising->setMaxInterval(48);
		advertising->start();
		delay(kAckIntervalMs * kAckAdvertisementCount);
		advertising->stop();
		epaper_timing_last.ble_ack_tx_ms = millis() - started;
		LOGI("Epaper", "BLE assignment ACK complete: rev=%lu elapsed=%lums",
			(unsigned long)packet.revision,
			(unsigned long)epaper_timing_last.ble_ack_tx_ms);
	} else {
		LOGW("Epaper", "BLE assignment advertiser unavailable; ACK not sent");
	}
	stop_ble();
}

EpaperBleFrameWakeResult epaper_ble_frame_try(DeviceConfig* config,
		EpaperBleFrameSelection* selection_out) {
	if (!config || !selection_out) return EpaperBleFrameWakeResult::HttpFallback;
	*selection_out = {};
	char device_id[64] = {};
	char api_key[128] = {};
	if (!epaper_assignment_extract_credentials(
			g_epaper_config.assignment_source_url, device_id, sizeof(device_id),
			api_key, sizeof(api_key))) {
		LOGW("Epaper", "BLE assignment disabled for this wake: assignment credentials unavailable");
		return EpaperBleFrameWakeResult::HttpFallback;
	}
	s_device_key = epaper_ble_device_key(device_id);
	EpaperAssignmentState accepted = {};
	epaper_assignment_load_state(&accepted);
	if (!scan(selection_out)) {
		stop_ble();
		epaper_timing_last.ble_path = EpaperBlePath::BleMissFallback;
		return EpaperBleFrameWakeResult::HttpFallback;
	}

	const EpaperBleFrameAction action = epaper_ble_frame_action(
		*selection_out, accepted, true);
	if (action == EpaperBleFrameAction::AcceptUnchanged) {
		LOGI("Epaper", "BLE assignment unchanged: rev=%lu crc=0x%08lx; accepting without WiFi",
			(unsigned long)selection_out->packet.revision,
			(unsigned long)selection_out->packet.content_crc32);
		epaper_assignment_accept_state(accepted_state(selection_out->packet));
		epaper_ble_frame_ack(selection_out->packet);
		epaper_timing_last.ble_path = EpaperBlePath::BleHit;
		return EpaperBleFrameWakeResult::AcceptedUnchanged;
	}
	if (action != EpaperBleFrameAction::RenderCached) {
		log_http_fallback_reason(*selection_out, accepted);
		stop_ble();
		epaper_timing_last.ble_path = EpaperBlePath::BleMissFallback;
		return EpaperBleFrameWakeResult::HttpFallback;
	}

	char image_key[17];
	image_key_hex(selection_out->packet.image_key, image_key);
	const char* format = selection_out->packet.image_format == EPAPER_BLE_FORMAT_JPEG
		? "jpeg" : "g16z";
	LOGI("Epaper", "BLE assignment cache lookup: rev=%lu key=%s format=%s crc=0x%08lx",
		(unsigned long)selection_out->packet.revision, image_key, format,
		(unsigned long)selection_out->packet.content_crc32);
	epaper_sd_cache_set_assignment_context(image_key,
		selection_out->packet.content_crc32, format);
	epaper_assignment_expect_transport_crc(selection_out->packet.content_crc32);
	epaper_sd_cache_set_cache_only(true);
	const EpaperRefreshOutcome outcome =
		epaper_refresh_show_assignment_cache(config);
	epaper_sd_cache_set_cache_only(false);
	epaper_assignment_expect_transport_crc(0);
	epaper_sd_cache_set_assignment_context(nullptr, 0, nullptr);
	if (outcome.result != EpaperRefreshResult::Updated) {
		LOGI("Epaper", "BLE assignment cache unavailable for rev=%lu; using HTTP fallback",
			(unsigned long)selection_out->packet.revision);
		stop_ble();
		epaper_timing_last.ble_path = EpaperBlePath::BleMissFallback;
		return EpaperBleFrameWakeResult::HttpFallback;
	}
	LOGI("Epaper", "BLE assignment rendered from cache: rev=%lu; accepting without WiFi",
		(unsigned long)selection_out->packet.revision);
	epaper_assignment_accept_state(accepted_state(selection_out->packet));
	epaper_ble_frame_ack(selection_out->packet);
	epaper_timing_last.ble_path = EpaperBlePath::BleHit;
	return EpaperBleFrameWakeResult::RenderedCached;
}

#endif
