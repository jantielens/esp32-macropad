#ifndef EPAPER_SCHEDULE_H
#define EPAPER_SCHEDULE_H

#include "board_config.h"

#if HAS_EPAPER

#include <stdint.h>

// Minimum valid Unix epoch (2000-01-01 00:00:00 UTC).
// Used to detect stale clocks that have not yet synced with NTP.
static const uint64_t EPAPER_SCHEDULE_MIN_VALID_EPOCH = 946684800;

// Evaluate whether a refresh should proceed based on the schedule.
// Pure function with no side effects.
//
// Fail-open: if the clock is stale (utc_epoch < MIN_VALID_EPOCH), returns true.
// This prevents the device from ever going into eternal sleep due to NTP timeout.
//
// @param hour_bitmask   24-bit mask of enabled hours (bit N = hour N is enabled).
//                        0x00FFFFFF = all hours enabled (schedule inactive).
// @param tz_offset      UTC offset in hours (-12 to +14) for local time.
// @param utc_epoch      Current Unix epoch (seconds since 1970-01-01 00:00:00 UTC).
//
// @return               true if the current hour is enabled (or clock is stale),
//                       false if the current hour is disabled.
bool epaper_schedule_should_refresh(uint32_t hour_bitmask, int8_t tz_offset, uint64_t utc_epoch);

// Compute seconds until the next enabled hour.
// Pure function with no side effects.
//
// @param hour_bitmask   24-bit mask of enabled hours.
// @param tz_offset      UTC offset in hours.
// @param utc_epoch      Current Unix epoch.
//
// @return               Seconds from now until the START of the next enabled hour.
//                       If no hours are enabled (mask=0), returns 3600 (1 hour).
//                       Used to set the sleep duration when schedule is disabled.
uint32_t epaper_schedule_seconds_to_next(uint32_t hour_bitmask, int8_t tz_offset, uint64_t utc_epoch);

#endif // HAS_EPAPER

#endif // EPAPER_SCHEDULE_H
