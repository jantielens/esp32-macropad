#include "board_config.h"

#if HAS_TOUCH

#include "touch_manager.h"
#include "log_manager.h"

// Touch init may run while the LVGL rendering task is active.
// LVGL is not thread-safe, so guard LVGL API calls with the DisplayManager mutex when available.
#if HAS_DISPLAY
#include "display_manager.h"
#include "screen_saver_manager.h"
#endif

// Include selected touch driver header.
// Driver implementations are compiled via src/app/touch_drivers.cpp.
#if TOUCH_DRIVER == TOUCH_DRIVER_XPT2046
#include "drivers/xpt2046_driver.h"
#elif TOUCH_DRIVER == TOUCH_DRIVER_AXS15231B_I2C
#include "drivers/axs15231b_touch_driver.h"
#elif TOUCH_DRIVER == TOUCH_DRIVER_CST816S_WIRE
#include "drivers/wire_cst816s_touch_driver.h"
#elif TOUCH_DRIVER == TOUCH_DRIVER_GT911
#include "drivers/gt911_touch_driver.h"
#endif

// Global instance
TouchManager* touchManager = nullptr;

// When set, LVGL will see touch as released until this timestamp.
static uint32_t g_lvgl_suppress_until_ms = 0;
static bool g_lvgl_force_released = false;
static bool g_prev_lvgl_pressed = false;

TouchManager::TouchManager() 
		: driver(nullptr), indev(nullptr), lvglRegisterPending(false) {
		// Driver will be instantiated in init() after display is ready
}

TouchManager::~TouchManager() {
		if (driver) {
				delete driver;
				driver = nullptr;
		}
}

void TouchManager::readCallback(lv_indev_t* indev, lv_indev_data_t* data) {
		TouchManager* manager = (TouchManager*)lv_indev_get_user_data(indev);

		// Suppress LVGL touch input for a short grace window (e.g., wake-tap swallow).
		const uint32_t now = millis();
		if (g_lvgl_force_released || ((int32_t)(g_lvgl_suppress_until_ms - now) > 0)) {
				data->state = LV_INDEV_STATE_RELEASED;
				g_prev_lvgl_pressed = false;
				return;
		}
		
		uint16_t x, y;
		if (manager->driver->getTouch(&x, &y)) {
				data->state = LV_INDEV_STATE_PRESSED;
				data->point.x = x;
				data->point.y = y;

				// Any real user press counts as activity; this keeps the idle timer from
				// expiring while the user is actively navigating the UI.
				const bool pressedEdge = !g_prev_lvgl_pressed;
				g_prev_lvgl_pressed = true;
				if (pressedEdge) {
						#if HAS_DISPLAY
						screen_saver_manager_notify_activity(false);
						#endif
				}
		} else {
				data->state = LV_INDEV_STATE_RELEASED;
				g_prev_lvgl_pressed = false;
		}
}

void TouchManager::init() {
		LOGI("Touch", "Manager init start");
		
		// Create standalone touch driver (no dependency on display)
		#if TOUCH_DRIVER == TOUCH_DRIVER_XPT2046
		driver = new XPT2046_Driver(TOUCH_CS, TOUCH_IRQ);
		#elif TOUCH_DRIVER == TOUCH_DRIVER_AXS15231B_I2C
		driver = new AXS15231B_TouchDriver();
		#elif TOUCH_DRIVER == TOUCH_DRIVER_CST816S_WIRE
		driver = new Wire_CST816S_TouchDriver();
		#elif TOUCH_DRIVER == TOUCH_DRIVER_GT911
		driver = new GT911_TouchDriver();
		#else
		#error "No touch driver selected or unknown driver type"
		#endif
		
		// Initialize hardware
		driver->init();
		
		// Set calibration if defined
		#if defined(TOUCH_CAL_X_MIN) && defined(TOUCH_CAL_X_MAX) && defined(TOUCH_CAL_Y_MIN) && defined(TOUCH_CAL_Y_MAX)
		driver->setCalibration(TOUCH_CAL_X_MIN, TOUCH_CAL_X_MAX, TOUCH_CAL_Y_MIN, TOUCH_CAL_Y_MAX);
		#endif
		
		// Set rotation to match display
		#ifdef DISPLAY_ROTATION
		driver->setRotation(DISPLAY_ROTATION);
		LOGI("Touch", "Rotation: %d", DISPLAY_ROTATION);
		#endif

		// Register with LVGL as input device.
		// Do NOT block boot indefinitely if the LVGL task/mutex is stuck; defer and retry.
		lvglRegisterPending = true;
		if (tryRegisterWithLVGL()) {
				LOGI("Touch", "Input device registered with LVGL");
		} else {
				LOGW("Touch", "LVGL registration deferred (LVGL busy)");
		}
		LOGI("Touch", "Manager init complete");
}

bool TouchManager::tryRegisterWithLVGL() {
		if (!lvglRegisterPending) return true;
		if (indev) {
				lvglRegisterPending = false;
				return true;
		}

		bool locked = true;
		#if HAS_DISPLAY
		locked = display_manager_try_lock(50);
		#endif

		if (!locked) {
				return false;
		}

		// v9 indev API: create → set type → set read_cb → set user_data
		indev = lv_indev_create();
		if (indev) {
				lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
				lv_indev_set_read_cb(indev, TouchManager::readCallback);
				lv_indev_set_user_data(indev, this);
		}

		#if HAS_DISPLAY
		display_manager_unlock();
		#endif

		lvglRegisterPending = (indev == nullptr);
		return indev != nullptr;
}

void TouchManager::loop() {
		(void)tryRegisterWithLVGL();
}

bool TouchManager::isTouched() {
		return driver->isTouched();
}

bool TouchManager::getTouch(uint16_t* x, uint16_t* y) {
		return driver->getTouch(x, y);
}

// C-style interface for app.ino
void touch_manager_init() {
		if (!touchManager) {
				touchManager = new TouchManager();
		}
		touchManager->init();
}

void touch_manager_loop() {
		if (!touchManager) return;
		touchManager->loop();
}

bool touch_manager_is_touched() {
		if (!touchManager) return false;
		return touchManager->isTouched();
}

void touch_manager_suppress_lvgl_input(uint32_t duration_ms) {
		const uint32_t now = millis();
		const uint32_t until = now + duration_ms;
		// Extend suppression window if already active.
		if ((int32_t)(g_lvgl_suppress_until_ms - until) < 0) {
				g_lvgl_suppress_until_ms = until;
		}
}

void touch_manager_set_lvgl_force_released(bool force_released) {
		g_lvgl_force_released = force_released;
}

#endif // HAS_TOUCH
