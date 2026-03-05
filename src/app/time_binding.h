#pragma once

// Register the "time" binding scheme with the binding template engine.
// Provides [time:format] and [time:format;timezone] tokens for displaying
// the current date/time.  Requires NTP sync (call time_binding_start_ntp()
// after WiFi connects).  Call time_binding_init() once during setup().
void time_binding_init();

// Start NTP time sync.  Call once after WiFi is connected.
// Uses pool.ntp.org with UTC as the default timezone.
void time_binding_start_ntp();
