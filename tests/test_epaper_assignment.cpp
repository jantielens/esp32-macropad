#include "device_classes/epaper/epaper_assignment_logic.h"

#include <cassert>
#include <cstring>
#include <iostream>

enum class CharacterizedRefreshResult {
		Unchanged,
		RedrawSynchronizedCache,
		RedrawSynchronizedDownload,
		RedrawAcceptedCache,
		RedrawAcceptedDownload,
		Failed,
};

struct CharacterizedRefreshCase {
		const char* name;
		bool force;
		bool sync_succeeded;
		bool accepted_state_present;
		bool content_unchanged;
		bool cache_valid;
		bool body_available;
		CharacterizedRefreshResult expected;
};

static CharacterizedRefreshResult characterize_current_refresh(
		const CharacterizedRefreshCase& test) {
		const EpaperAssignmentRefreshAction refresh_action = epaper_assignment_refresh_action(
			test.force, test.sync_succeeded, test.accepted_state_present,
			test.content_unchanged);
		if (refresh_action == EpaperAssignmentRefreshAction::Fail) {
				return CharacterizedRefreshResult::Failed;
		}
		if (refresh_action == EpaperAssignmentRefreshAction::SkipUnchanged) {
				return CharacterizedRefreshResult::Unchanged;
		}
		const EpaperAssignmentTransportAction transport_action =
			epaper_assignment_transport_action(test.cache_valid);
		if (transport_action == EpaperAssignmentTransportAction::Download &&
			!test.body_available) {
				return CharacterizedRefreshResult::Failed;
		}
		if (refresh_action == EpaperAssignmentRefreshAction::UseAccepted) {
				return transport_action == EpaperAssignmentTransportAction::UseCache
						? CharacterizedRefreshResult::RedrawAcceptedCache
						: CharacterizedRefreshResult::RedrawAcceptedDownload;
		}
		return transport_action == EpaperAssignmentTransportAction::UseCache
				? CharacterizedRefreshResult::RedrawSynchronizedCache
				: CharacterizedRefreshResult::RedrawSynchronizedDownload;
}

static void characterize_refresh_truth_table() {
		const CharacterizedRefreshCase cases[] = {
				{"scheduled unchanged", false, true, true, true, true, true,
					CharacterizedRefreshResult::Unchanged},
				{"scheduled changed cache valid", false, true, true, false, true, true,
					CharacterizedRefreshResult::RedrawSynchronizedCache},
				{"scheduled changed cache missing", false, true, true, false, false, true,
					CharacterizedRefreshResult::RedrawSynchronizedDownload},
				{"scheduled first assignment", false, true, false, false, false, true,
					CharacterizedRefreshResult::RedrawSynchronizedDownload},
				{"scheduled sync failure with accepted state", false, false, true, true, true, true,
					CharacterizedRefreshResult::Failed},
				{"scheduled sync failure without accepted state", false, false, false, false, false, false,
					CharacterizedRefreshResult::Failed},
				{"forced unchanged cache valid", true, true, true, true, true, true,
					CharacterizedRefreshResult::RedrawSynchronizedCache},
				{"forced unchanged cache corrupt", true, true, true, true, false, true,
					CharacterizedRefreshResult::RedrawSynchronizedDownload},
				{"forced newer pending", true, true, true, false, false, true,
					CharacterizedRefreshResult::RedrawSynchronizedDownload},
				{"forced synchronized cache missing and WiFi unavailable", true, true, true, false, false, false,
					CharacterizedRefreshResult::Failed},
				{"forced sync failure accepted cache valid", true, false, true, true, true, false,
					CharacterizedRefreshResult::RedrawAcceptedCache},
				{"forced sync failure accepted cache missing transport available", true, false, true, true, false, true,
					CharacterizedRefreshResult::RedrawAcceptedDownload},
				{"forced WiFi unavailable accepted cache corrupt", true, false, true, true, false, false,
					CharacterizedRefreshResult::Failed},
				{"forced sync failure without accepted state", true, false, false, false, true, true,
					CharacterizedRefreshResult::Failed},
		};

		for (const CharacterizedRefreshCase& test : cases) {
				const CharacterizedRefreshResult actual = characterize_current_refresh(test);
				assert(actual == test.expected);
				if (!test.sync_succeeded) {
						assert(actual != CharacterizedRefreshResult::Unchanged);
				}
				if (!test.cache_valid && !test.body_available) {
						assert(actual == CharacterizedRefreshResult::Failed);
				}
		}
}

static void check_url(const char* carousel, const char* override_url,
		const char* action, uint32_t revision, const char* expected) {
		char result[384] = {};
		assert(epaper_assignment_build_url(carousel, override_url, action,
			revision, result, sizeof(result)));
		assert(std::strcmp(result, expected) == 0);
}

int main() {
		static_assert(sizeof(EpaperAssignmentState) == 16, "packed NVS state changed");
		characterize_refresh_truth_table();

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
