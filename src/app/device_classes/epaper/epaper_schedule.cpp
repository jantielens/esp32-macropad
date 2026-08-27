#include "epaper_schedule.h"

#if HAS_EPAPER

bool epaper_schedule_should_refresh(uint32_t hour_bitmask, int8_t tz_offset, uint64_t utc_epoch) {
		// Fail-open: if clock is stale, allow refresh
		if (utc_epoch < EPAPER_SCHEDULE_MIN_VALID_EPOCH) {
				return true;
		}

		// Compute local hour
		// utc_epoch / 3600 = hours since 1970-01-01 00:00:00 UTC
		// Add tz_offset to shift to local time
		// Modulo 24 to get hour-of-day (0-23)
		uint32_t utc_hour = (utc_epoch / 3600u);
		int32_t local_hour_signed = (int32_t)utc_hour + (int32_t)tz_offset;
		// Handle negative modulo: -1 % 24 should be 23, not -1
		int32_t local_hour = ((local_hour_signed % 24) + 24) % 24;

		// Check if this hour is enabled (bit local_hour set in mask)
		return (hour_bitmask & (1U << local_hour)) != 0;
}

uint32_t epaper_schedule_seconds_to_next(uint32_t hour_bitmask, int8_t tz_offset, uint64_t utc_epoch) {
		// Safety: if no hours are enabled, sleep for 1 hour
		if (hour_bitmask == 0) {
				return 3600;
		}

		// Current local hour
		uint32_t utc_hour = (utc_epoch / 3600u);
		int32_t local_hour_signed = (int32_t)utc_hour + (int32_t)tz_offset;
		int32_t current_local_hour = ((local_hour_signed % 24) + 24) % 24;

		// Find the next enabled hour by iterating 1-24 hours forward
		for (int i = 1; i <= 24; ++i) {
				int32_t test_hour = (current_local_hour + i) % 24;
				if (hour_bitmask & (1U << test_hour)) {
						// Found the next enabled hour
						// Compute its Unix epoch (start of that hour)
						// Current UTC time in seconds: utc_epoch
						// Seconds into the current hour: utc_epoch % 3600
						// Seconds until start of next hour: 3600 - (utc_epoch % 3600)
						// Then add (i-1) * 3600 for each full hour until the target hour

						uint32_t seconds_into_current_hour = utc_epoch % 3600u;
						uint32_t seconds_to_next_hour = 3600u - seconds_into_current_hour;

						// Add full hours between current and target
						uint32_t total_seconds = seconds_to_next_hour + (uint32_t)(i - 1) * 3600u;

						// If we're at the start of an hour (seconds_into_current_hour == 0)
						// and the current hour is enabled, return 0 (but caller should have checked already)
						return total_seconds;
				}
		}

		// Should not reach here (loop always finds at least the current hour wrapping around)
		return 3600;
}

#endif // HAS_EPAPER
