#include "board_config.h"

#if HAS_TOUCH

#include "touch_manager.h"
#include "log_manager.h"

#include <atomic>

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
static std::atomic<uint32_t> g_lvgl_suppress_until_ms{0};
static std::atomic<bool> g_lvgl_force_released{false};
static bool g_prev_lvgl_pressed = false;
// After suppression ends, require a genuine release before accepting new presses.
// This prevents stale "touched" state in drivers (e.g., GT911 lastTouched flag)
// from replaying as a phantom click.
static bool g_require_release = false;

#if HAS_DISPLAY
enum class SyntheticTapState : uint8_t {
		Idle,
		PendingPress,
		ReleaseOwed,
};

struct SyntheticTapSlot {
		SyntheticTapState state;
		uint16_t x;
		uint16_t y;
		uint32_t queued_at_ms;
};

static constexpr uint32_t SYNTHETIC_TAP_PENDING_TIMEOUT_MS = 5000;
static portMUX_TYPE g_synthetic_tap_mux = portMUX_INITIALIZER_UNLOCKED;
static SyntheticTapSlot g_synthetic_tap = {SyntheticTapState::Idle, 0, 0, 0};

static bool synthetic_tap_take_release_owed() {
		bool releaseOwed = false;
		portENTER_CRITICAL(&g_synthetic_tap_mux);
		if (g_synthetic_tap.state == SyntheticTapState::ReleaseOwed) {
			g_synthetic_tap.state = SyntheticTapState::Idle;
			releaseOwed = true;
		}
		portEXIT_CRITICAL(&g_synthetic_tap_mux);
		return releaseOwed;
}

static bool synthetic_tap_expire_pending(uint32_t now) {
		bool expired = false;
		portENTER_CRITICAL(&g_synthetic_tap_mux);
		if (g_synthetic_tap.state == SyntheticTapState::PendingPress &&
				(uint32_t)(now - g_synthetic_tap.queued_at_ms) >= SYNTHETIC_TAP_PENDING_TIMEOUT_MS) {
			g_synthetic_tap.state = SyntheticTapState::Idle;
			expired = true;
		}
		portEXIT_CRITICAL(&g_synthetic_tap_mux);
		return expired;
}

static bool synthetic_tap_take_pending_press(uint16_t* x, uint16_t* y) {
		bool pressed = false;
		portENTER_CRITICAL(&g_synthetic_tap_mux);
		if (g_synthetic_tap.state == SyntheticTapState::PendingPress) {
			*x = g_synthetic_tap.x;
			*y = g_synthetic_tap.y;
			g_synthetic_tap.state = SyntheticTapState::ReleaseOwed;
			pressed = true;
		}
		portEXIT_CRITICAL(&g_synthetic_tap_mux);
		return pressed;
}
#endif

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

		const uint32_t now = millis();
		#if HAS_DISPLAY
		// An emitted synthetic press always gets this next-callback release, before
		// suppression or physical input can produce another pointer state.
		if (synthetic_tap_take_release_owed()) {
			data->state = LV_INDEV_STATE_RELEASED;
			g_prev_lvgl_pressed = false;
			return;
		}
		synthetic_tap_expire_pending(now);
		#endif

		const bool forceReleased = g_lvgl_force_released.load();
		const uint32_t suppressUntil = g_lvgl_suppress_until_ms.load();
		if (forceReleased || ((int32_t)(suppressUntil - now) > 0)) {
				data->state = LV_INDEV_STATE_RELEASED;
				g_prev_lvgl_pressed = false;
				g_require_release = true;
				return;
		}
		
		uint16_t x, y;
		const bool touched = manager->driver->getTouch(&x, &y);

		// After suppression ends, wait for a genuine release before forwarding presses.
		// Drivers like GT911 can retain stale "touched" state across the suppression window.
		if (g_require_release) {
				if (!touched) {
						g_require_release = false;
				}
				data->state = LV_INDEV_STATE_RELEASED;
				g_prev_lvgl_pressed = false;
				return;
		}

		#if HAS_DISPLAY
		if (!touched && screen_saver_manager_input_ready()) {
			uint16_t tapX = 0;
			uint16_t tapY = 0;
			if (synthetic_tap_take_pending_press(&tapX, &tapY)) {
				data->state = LV_INDEV_STATE_PRESSED;
				data->point.x = tapX;
				data->point.y = tapY;
				g_prev_lvgl_pressed = true;
				return;
			}
		}
		#endif

		if (touched) {
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
		uint32_t current = g_lvgl_suppress_until_ms.load();
		while ((int32_t)(current - until) < 0 &&
				!g_lvgl_suppress_until_ms.compare_exchange_weak(current, until)) {}
}

void touch_manager_set_lvgl_force_released(bool force_released) {
		g_lvgl_force_released.store(force_released);
}

#if HAS_DISPLAY
TouchManagerEnqueueResult touch_manager_enqueue_tap(int32_t x, int32_t y) {
		if (!touchManager || !touchManager->isReady() || !displayManager) {
			return TOUCH_MANAGER_ENQUEUE_UNAVAILABLE;
		}

		const int width = displayManager->getActiveWidth();
		const int height = displayManager->getActiveHeight();
		if (x < 0 || y < 0 || x >= width || y >= height) {
			return TOUCH_MANAGER_ENQUEUE_INVALID;
		}

		portENTER_CRITICAL(&g_synthetic_tap_mux);
		if (g_synthetic_tap.state != SyntheticTapState::Idle) {
			portEXIT_CRITICAL(&g_synthetic_tap_mux);
			return TOUCH_MANAGER_ENQUEUE_BUSY;
		}
		g_synthetic_tap.state = SyntheticTapState::PendingPress;
		g_synthetic_tap.x = (uint16_t)x;
		g_synthetic_tap.y = (uint16_t)y;
		g_synthetic_tap.queued_at_ms = millis();
		portEXIT_CRITICAL(&g_synthetic_tap_mux);

		screen_saver_manager_notify_activity(true);
		return TOUCH_MANAGER_ENQUEUE_QUEUED;
}
#endif // HAS_DISPLAY

#endif // HAS_TOUCH
