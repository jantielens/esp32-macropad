// Display Manager - C-API wrappers for app.ino
#include "board_config.h"

#if HAS_DISPLAY

#include "display_manager.h"

// C-style interface for app.ino
void display_manager_init(DeviceConfig* config) {
		if (!displayManager) {
				displayManager = new DisplayManager(config);
				displayManager->init();
		}
}

void display_manager_show_splash() {
		if (displayManager) {
				displayManager->showSplash();
		}
}

void display_manager_show_info() {
		if (displayManager) {
				displayManager->showInfo();
		}
}

void display_manager_show_test() {
		if (displayManager) {
				displayManager->showTest();
		}
}

void display_manager_set_splash_status(const char* text) {
		if (displayManager) {
				displayManager->setSplashStatus(text);
		}
}

void display_manager_show_screen(const char* screen_id, bool* success) {
		bool result = false;
		if (displayManager) {
				result = displayManager->showScreen(screen_id);
		}
		if (success) *success = result;
}

bool display_manager_go_back() {
		if (displayManager) {
				return displayManager->goBack();
		}
		return false;
}

const char* display_manager_get_current_screen_id() {
		if (displayManager) {
				return displayManager->getCurrentScreenId();
		}
		return nullptr;
}

const ScreenInfo* display_manager_get_available_screens(size_t* count) {
		if (displayManager) {
				return displayManager->getAvailableScreens(count);
		}
		if (count) *count = 0;
		return nullptr;
}

void display_manager_set_backlight_brightness(uint8_t brightness) {
		if (displayManager && displayManager->getDriver()) {
				displayManager->getDriver()->setBacklightBrightness(brightness);
		}
}

void display_manager_lock() {
		if (displayManager) {
				displayManager->lock();
		}
}

void display_manager_unlock() {
		if (displayManager) {
				displayManager->unlock();
		}
}

bool display_manager_try_lock(uint32_t timeout_ms) {
		if (!displayManager) return false;
		return displayManager->tryLock(timeout_ms);
}

#endif // HAS_DISPLAY
