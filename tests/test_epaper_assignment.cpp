#include "device_classes/epaper/epaper_assignment_logic.h"

#include <cassert>
#include <cstring>
#include <iostream>

static void check_url(const char* carousel, const char* override_url,
		const char* action, uint32_t revision, const char* expected) {
		char result[384] = {};
		assert(epaper_assignment_build_url(carousel, override_url, action,
			revision, result, sizeof(result)));
		assert(std::strcmp(result, expected) == 0);
}

int main() {
		static_assert(sizeof(EpaperAssignmentState) == 16, "packed NVS state changed");

		check_url("https://frame.test/api/next?device_id=one&key=secret", "",
			"sync", 0,
			"https://frame.test/api/assignment/sync?device_id=one&key=secret");
		check_url("https://frame.test/api/next?device_id=one&key=secret", "",
			"image", 42,
			"https://frame.test/api/assignment/image?device_id=one&key=secret&revision=42");
		check_url("https://legacy.test/api/next?device_id=one&key=secret",
			"https://assign.test", "sync", 0,
			"https://assign.test/api/assignment/sync?device_id=one&key=secret");
		check_url("https://legacy.test/api/next?device_id=one&key=secret",
			"https://assign.test/api/assignment/current?device_id=two&key=other",
			"image", 7,
			"https://assign.test/api/assignment/image?device_id=two&key=other&revision=7");

		char short_buffer[16] = {};
		assert(!epaper_assignment_build_url(
			"https://frame.test/api/next?device_id=one&key=secret", "", "sync",
			0, short_buffer, sizeof(short_buffer)));
		char result[128] = {};
		assert(!epaper_assignment_build_url("https://frame.test/image.jpg", "",
			"sync", 0, result, sizeof(result)));

		assert(epaper_assignment_revision_newer(2, 1));
		assert(!epaper_assignment_revision_newer(1, 1));
		assert(epaper_assignment_revision_newer(1, 0xFFFFFFFFU));
		assert(!epaper_assignment_revision_newer(0xFFFFFFFFU, 1));
		assert(!epaper_assignment_revision_newer(0x80000001U, 1));

		assert(epaper_assignment_crc_allows_reuse(0x12345678U, 0x12345678U));
		assert(!epaper_assignment_crc_allows_reuse(0, 0));
		assert(!epaper_assignment_crc_allows_reuse(0, 0x12345678U));
		assert(!epaper_assignment_crc_allows_reuse(0x12345678U, 0x87654321U));

		const uint8_t crc_vector[] = "123456789";
		assert(epaper_assignment_transport_crc32(crc_vector, 9) == 0xCBF43926U);
		epaper_assignment_expect_transport_crc(0xCBF43926U);
		assert(epaper_assignment_validate_transport(crc_vector, 9));
		epaper_assignment_expect_transport_crc(0x12345678U);
		assert(!epaper_assignment_validate_transport(crc_vector, 9));
		epaper_assignment_expect_transport_crc(0);
		assert(epaper_assignment_validate_transport(crc_vector, 9));

		std::cout << "OK: epaper assignment logic checks passed\n";
		return 0;
}
