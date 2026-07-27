#include "device_classes/epaper/epaper_next_client_logic.h"
#include "device_classes/epaper/epaper_transport_crc32.h"

#include <assert.h>
#include <stdint.h>

int main() {
		assert(!epaper_source_uses_service(EpaperSourceMode::SlotCarousel));
		assert(epaper_source_uses_service(EpaperSourceMode::Service));
		assert(epaper_source_advances_carousel(EpaperSourceMode::SlotCarousel, 2));
		assert(!epaper_source_advances_carousel(EpaperSourceMode::Service, 2));
		assert(epaper_source_refresh_interval(EpaperSourceMode::SlotCarousel, 60, 900) == 900);
		assert(epaper_source_refresh_interval(EpaperSourceMode::Service, 60, 900) == 60);

		assert(epaper_next_action_for_status(200) == EpaperNextAction::DownloadInline);
		assert(epaper_next_action_for_status(204) == EpaperNextAction::Keep);
		assert(epaper_next_action_for_status(302) == EpaperNextAction::FollowRedirect);
		assert(epaper_next_action_for_status(401) == EpaperNextAction::AuthFailed);
		assert(epaper_next_action_for_status(404) == EpaperNextAction::UnsupportedMajor);
		assert(epaper_next_action_for_status(503) == EpaperNextAction::TransientFailure);
		assert(epaper_next_action_for_status(301) == EpaperNextAction::InvalidResponse);
		assert(epaper_next_use_cached_blob(true, true));
		assert(!epaper_next_use_cached_blob(false, true));

		uint32_t crc = 1;
		assert(epaper_next_parse_crc32("00000000", &crc) && crc == 0);
		assert(epaper_next_parse_crc32("89abcdef", &crc) && crc == 0x89ABCDEFU);
		assert(!epaper_next_parse_crc32("89ABCDEF", &crc));
		assert(!epaper_next_parse_crc32("1234567", &crc));
		assert(epaper_next_valid_image_key("M7x4qQ2V0A"));
		assert(!epaper_next_valid_image_key("bad/key"));

		const uint8_t vector[] = "123456789";
		assert(epaper_transport_crc32(nullptr, 0) == 0);
		assert(epaper_transport_crc32(vector, 9) == 0xCBF43926U);
		return 0;
}