#pragma once

// Register the "time" binding scheme with the binding template engine.
// Provides [time:format] and [time:format;timezone] tokens for displaying
// the current date/time.  Requires NTP sync (call time_binding_start_ntp()
// after WiFi connects).  Call time_binding_init() once during setup().
//
// Standard strftime codes are supported plus custom sub-second extensions:
//   %ms  — milliseconds within second (000-999, 1ms granularity)
//   %cs  — centiseconds (00-99, 10ms granularity)
//   %ds  — deciseconds (0-9, 100ms granularity)
//   %ums — device uptime in milliseconds (standalone, no NTP needed)
void time_binding_init();

// Start NTP time sync.  Call once after WiFi is connected.
// Uses pool.ntp.org with UTC as the default timezone.
void time_binding_start_ntp();
