#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include "board_config.h"
#include "config_manager.h"
#include "display_driver.h"
#include "screens/screen.h"
#include "screens/splash_screen.h"
#include "screens/info_screen.h"
#include "screens/test_screen.h"
#include "screens/fps_screen.h"

#if HAS_TOUCH && LV_USE_CANVAS
#include "screens/touch_test_screen.h"
#endif

#include <lvgl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "rtos_task_utils.h"

// ============================================================================
// Screen Registry
// ============================================================================
// Maximum number of screens that can be registered for runtime navigation
// Generous headroom (8 slots) allows adding new screens without recompiling
// Only ~192 bytes total (24 bytes × 8), negligible overhead vs heap allocation
#define MAX_SCREENS 8

// Struct for registering available screens dynamically
struct ScreenInfo {
		const char* id;            // Unique identifier (e.g., "info", "test")
		const char* display_name;  // Human-readable name (e.g., "Info Screen")
		Screen* instance;          // Pointer to screen instance
};

// ============================================================================
// Display Manager
// ============================================================================
// Manages display hardware, LVGL, screen lifecycle, and navigation.
// Uses FreeRTOS task for continuous LVGL rendering (works on single and dual core).
//
// Usage:
//   display_manager_init(&device_config);  // In setup() - starts rendering task
//   display_manager_show_main();           // When WiFi connected
//   display_manager_set_splash_status();   // Update splash text
//
// Note: No need to call update() in loop() - rendering task handles it

class DisplayManager {
private:
		// Hardware (display driver abstraction)
		DisplayDriver* driver;
		lv_display_t* display;    // v9 display object
		uint8_t* buf;             // Dynamically allocated LVGL draw buffer
		uint8_t* buf2;            // Optional second draw buffer (double-buffering)
		
		// Configuration reference
		DeviceConfig* config;
		
		// FreeRTOS task and mutex
		TaskHandle_t lvglTaskHandle;
		RtosTaskPsramAlloc lvglTaskAlloc;  // PSRAM stack allocation (if used)
		SemaphoreHandle_t lvglMutex;
		
		// Async present task (Buffered render mode only).
		// Decouples the slow panel transfer from the LVGL timer loop
		// so touch input and animations can process at ~50 Hz.
		TaskHandle_t presentTaskHandle;
		RtosTaskPsramAlloc presentTaskAlloc;
		SemaphoreHandle_t presentSem;
		volatile uint32_t sharedLvTimerUs;  // Latest lv_timer_handler() duration for perf stats
		
		// Screen management
		Screen* currentScreen;
		Screen* previousScreen;  // Track previous screen for return navigation
		Screen* pendingScreen;   // Deferred screen switch (processed in lvglTask)

		// Defer small LVGL UI updates (like splash status) to the LVGL task.
		char pendingSplashStatus[96];
		volatile bool pendingSplashStatusSet;

		// Helpers: avoid taking the LVGL mutex when already inside the LVGL task
		bool isInLvglTask() const;
		void lockIfNeeded(bool& didLock);
		void unlockIfNeeded(bool didLock);
		
		// Screen instances (created at init, kept in memory)
		SplashScreen splashScreen;
		InfoScreen infoScreen;
		TestScreen testScreen;
		FpsScreen fpsScreen;
		
		#if HAS_TOUCH && LV_USE_CANVAS
		TouchTestScreen touchTestScreen;
		#endif
		
		// Screen registry for runtime navigation (static allocation, no heap)
		// screenCount tracks how many slots are actually used (currently 2: info, test)
		// Splash excluded from runtime selection (boot-specific only)
		ScreenInfo availableScreens[MAX_SCREENS];
		size_t screenCount;

		// Internal helper: map a Screen instance to its logical screen id.
		// Uses the registered screen list so adding new screens doesn't require
		// updating logging code.
		const char* getScreenIdForInstance(const Screen* screen) const;
		
		// Hardware initialization
		void initHardware();
		void initLVGL();
		
		// LVGL flush callback (static, accesses instance via user_data)
		static void flushCallback(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);

		// Buffered render-mode drivers (e.g., Arduino_GFX canvas) need an explicit
		// present() step, but only after LVGL has actually rendered something.
		bool flushPending;

		// FreeRTOS task for LVGL rendering
		static void lvglTask(void* pvParameter);
		
		// FreeRTOS task for async panel transfer (Buffered render mode only)
		static void presentTask(void* pvParameter);
		
public:
		DisplayManager(DeviceConfig* config);
		~DisplayManager();
		
		// Initialize hardware + LVGL + screens + rendering task (shows splash automatically)
		void init();
		
		// Navigation API (thread-safe)
		void showSplash();
		void showInfo();
		void showTest();
		
		// Screen selection by ID (thread-safe, returns true if found)
		bool showScreen(const char* screen_id);
		
		// Get current screen ID (returns nullptr if splash or no screen)
		const char* getCurrentScreenId();
		
		// Get available screens for runtime navigation
		const ScreenInfo* getAvailableScreens(size_t* count);
		
		// Splash status update (thread-safe)
		void setSplashStatus(const char* text);
		
		// Mutex helpers for external thread-safe access
		void lock();
		void unlock();

		// Attempt to lock the LVGL mutex with a timeout (in milliseconds).
		// Returns true if the lock was acquired.
		bool tryLock(uint32_t timeoutMs);

		// Active LVGL logical resolution (post driver->configureLVGL()).
		// Prefer using these instead of calling LVGL APIs from non-LVGL tasks.
	int getActiveWidth() const;
	int getActiveHeight() const;
		SplashScreen* getSplash() { return &splashScreen; }
		
		// Access to display driver (for touch integration)
		DisplayDriver* getDriver() { return driver; }
};

// Lightweight rendering/perf snapshot (best-effort).
struct DisplayPerfStats {
		uint16_t fps;
		uint32_t lv_timer_us;
		uint32_t present_us;
};

// Global instance (managed by app.ino)
extern DisplayManager* displayManager;

// C-style interface for app.ino
void display_manager_init(DeviceConfig* config);
void display_manager_show_splash();
void display_manager_show_info();
void display_manager_show_test();
void display_manager_show_screen(const char* screen_id, bool* success);  // success is optional output
const char* display_manager_get_current_screen_id();
const ScreenInfo* display_manager_get_available_screens(size_t* count);
void display_manager_set_splash_status(const char* text);
void display_manager_set_backlight_brightness(uint8_t brightness);  // 0-100%

// Serialization helpers for code running outside the LVGL task.
// Use these to avoid concurrent access to buffered display backends (e.g., Arduino_GFX canvas).
void display_manager_lock();
void display_manager_unlock();
bool display_manager_try_lock(uint32_t timeout_ms);

// Best-effort perf stats for diagnostics (/api/health).
// Returns false until a first stats window has been captured.
bool display_manager_get_perf_stats(DisplayPerfStats* out);

#endif // DISPLAY_MANAGER_H
