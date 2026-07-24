#pragma once

#include <stddef.h>
#include <stdint.h>

struct __attribute__((packed)) EpaperAssignmentState {
		uint32_t revision;
		uint8_t image_key[8];
		uint32_t content_crc32;
};
static_assert(sizeof(EpaperAssignmentState) == 16, "ep_assign must stay 16 bytes");

bool epaper_assignment_revision_newer(uint32_t left, uint32_t right);
bool epaper_assignment_crc_allows_reuse(uint32_t expected, uint32_t displayed);
uint32_t epaper_assignment_transport_crc32(const uint8_t* data, size_t len);
void epaper_assignment_expect_transport_crc(uint32_t expected);
bool epaper_assignment_validate_transport(const uint8_t* data, size_t len);

bool epaper_assignment_build_url(const char* carousel_url, const char* override_url,
		const char* action, uint32_t revision, char* out, size_t out_size);
