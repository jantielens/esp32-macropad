#ifndef SCREEN_SAVER_MANAGER_H
#define SCREEN_SAVER_MANAGER_H

#include "board_config.h"
#include "config_manager.h"

// Screen Saver Manager (v1)
// Implements inactivity-based display sleep by fading the backlight to 0.
// Wake sources: touch press (optional), API trigger, code trigger.

#if HAS_DISPLAY

#include <Arduino.h>

enum class ScreenSaverState : uint8_t {
		Awake = 0,
		FadingOut = 1,
		Asleep = 2,
		FadingIn = 3,
};

struct ScreenSaverStatus {
		bool enabled;
		ScreenSaverState state;
		uint8_t current_brightness;
		uint8_t target_brightness;
		uint32_t seconds_until_sleep;
};

// Initialize with the current config (must remain valid for lifetime)
void screen_saver_manager_init(DeviceConfig* config);

// Call frequently from loop()
void screen_saver_manager_loop();

// Activity resets the inactivity timer; optionally wakes immediately (with fade)
void screen_saver_manager_notify_activity(bool wake);

// Explicit controls
void screen_saver_manager_sleep_now();
void screen_saver_manager_wake();

bool screen_saver_manager_is_asleep();
ScreenSaverStatus screen_saver_manager_get_status();

#else

// No display: provide no-op stubs to keep app.ino clean.

enum class ScreenSaverState : uint8_t {
		Awake = 0,
		FadingOut = 1,
		Asleep = 2,
		FadingIn = 3,
};

struct ScreenSaverStatus {
		bool enabled;
		ScreenSaverState state;
		uint8_t current_brightness;
		uint8_t target_brightness;
		uint32_t seconds_until_sleep;
};

inline void screen_saver_manager_init(DeviceConfig*) {}
inline void screen_saver_manager_loop() {}
inline void screen_saver_manager_notify_activity(bool) {}
inline void screen_saver_manager_sleep_now() {}
inline void screen_saver_manager_wake() {}
inline bool screen_saver_manager_is_asleep() { return false; }
inline ScreenSaverStatus screen_saver_manager_get_status() { return {false, ScreenSaverState::Awake, 0, 0, 0}; }

#endif // HAS_DISPLAY

#endif // SCREEN_SAVER_MANAGER_H
