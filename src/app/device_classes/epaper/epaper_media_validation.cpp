#include "epaper_media_validation.h"

#include "epaper_transport_crc32.h"

#include <string.h>

namespace {

constexpr size_t kG16pHeaderSize = 18;

uint16_t media_read_le16(const uint8_t* data) {
		return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

uint32_t media_read_le32(const uint8_t* data) {
		return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
				((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

} // namespace

EpaperMediaValidation epaper_validate_g16p(const uint8_t* data, size_t len,
		uint16_t panel_width, uint16_t panel_height) {
		if (!data || len < kG16pHeaderSize || memcmp(data, "G16P", 4) != 0) {
				return EpaperMediaValidation::Malformed;
		}
		const uint16_t width = media_read_le16(data + 6);
		const uint16_t height = media_read_le16(data + 8);
		const uint32_t payload_length = media_read_le32(data + 10);
		const uint32_t payload_crc = media_read_le32(data + 14);
		if (data[4] != 1 || data[5] != 0 || (width & 1U) != 0) {
				return EpaperMediaValidation::Malformed;
		}
		if (width != panel_width || height != panel_height) {
				return EpaperMediaValidation::WrongGeometry;
		}
		const size_t expected_payload = (size_t)width * height / 2;
		if (payload_length != expected_payload || len != kG16pHeaderSize + expected_payload) {
				return EpaperMediaValidation::Malformed;
		}
		if (epaper_transport_crc32(data + kG16pHeaderSize, expected_payload) != payload_crc) {
				return EpaperMediaValidation::PayloadCrcMismatch;
		}
		return EpaperMediaValidation::Valid;
}

EpaperMediaValidation epaper_validate_jpeg(const uint8_t* data, size_t len,
		uint16_t panel_width, uint16_t panel_height) {
		if (!data || len < 4 || data[0] != 0xFF || data[1] != 0xD8) {
				return EpaperMediaValidation::Malformed;
		}
		size_t offset = 2;
		while (offset + 4 <= len) {
				if (data[offset] != 0xFF) return EpaperMediaValidation::Malformed;
				while (offset < len && data[offset] == 0xFF) ++offset;
				if (offset >= len) return EpaperMediaValidation::Malformed;
				const uint8_t marker = data[offset++];
				if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD9)) continue;
				if (offset + 2 > len) return EpaperMediaValidation::Malformed;
				const uint16_t segment_length = ((uint16_t)data[offset] << 8) | data[offset + 1];
				if (segment_length < 2 || offset + segment_length > len) {
						return EpaperMediaValidation::Malformed;
				}
				const bool is_sof = marker >= 0xC0 && marker <= 0xCF &&
						marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
				if (is_sof) {
						if (segment_length < 7) return EpaperMediaValidation::Malformed;
						if (marker != 0xC0) return EpaperMediaValidation::Unsupported;
						const uint16_t height = ((uint16_t)data[offset + 3] << 8) | data[offset + 4];
						const uint16_t width = ((uint16_t)data[offset + 5] << 8) | data[offset + 6];
						return width == panel_width && height == panel_height
								? EpaperMediaValidation::Valid : EpaperMediaValidation::WrongGeometry;
				}
				offset += segment_length;
		}
		return EpaperMediaValidation::Malformed;
}

EpaperMediaValidation epaper_validate_g16z_completion(int status, int done_status,
		size_t consumed_input, size_t input_length,
		size_t produced_output, size_t expected_output) {
		if (status != done_status || produced_output != expected_output) {
				return EpaperMediaValidation::Malformed;
		}
		return consumed_input == input_length
				? EpaperMediaValidation::Valid : EpaperMediaValidation::TrailingBytes;
}
