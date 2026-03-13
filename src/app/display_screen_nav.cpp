// Display Manager - Screen navigation, splash status, and screen history
#include "board_config.h"

#if HAS_DISPLAY

#include "display_manager.h"
#include "log_manager.h"

extern portMUX_TYPE g_splash_status_mux;

void DisplayManager::showSplash() {
		// Splash shown during init - can switch immediately (no task running yet)
		lock();
		if (currentScreen) {
				currentScreen->hide();
		}
		currentScreen = &splashScreen;
		currentScreen->show();
		unlock();
		LOGI("Display", "Switched to SplashScreen");
}

void DisplayManager::showInfo() {
		// Defer screen switch to lvglTask (non-blocking)
		pendingScreen = &infoScreen;
		LOGI("Display", "Queued switch to InfoScreen");
}

void DisplayManager::showTest() {
		// Defer screen switch to lvglTask (non-blocking)
		pendingScreen = &testScreen;
		LOGI("Display", "Queued switch to TestScreen");
}

void DisplayManager::setSplashStatus(const char* text) {
		// If called before the LVGL task exists (during early setup), update directly.
		// Otherwise, defer to the LVGL task to avoid cross-task LVGL calls.
		if (!lvglTaskHandle || isInLvglTask()) {
				bool didLock = false;
				lockIfNeeded(didLock);
				splashScreen.setStatus(text);
				unlockIfNeeded(didLock);
				return;
		}

		portENTER_CRITICAL(&g_splash_status_mux);
		strlcpy(pendingSplashStatus, text ? text : "", sizeof(pendingSplashStatus));
		pendingSplashStatusSet = true;
		portEXIT_CRITICAL(&g_splash_status_mux);
}

bool DisplayManager::showScreen(const char* screen_id) {
		if (!screen_id) return false;
		
		// Look up screen in registry
		for (size_t i = 0; i < screenCount; i++) {
				if (strcmp(availableScreens[i].id, screen_id) == 0) {
						// Defer screen switch to lvglTask (non-blocking)
						pendingScreen = availableScreens[i].instance;
						LOGI("Display", "Queued switch to screen: %s", screen_id);
						return true;
				}
		}
		
		LOGW("Display", "Screen not found: %s", screen_id);
		return false;
}

bool DisplayManager::goBack() {
		if (screenHistoryCount == 0) return false;
		pendingScreen = screenHistory[--screenHistoryCount];
		skipHistoryPush = true;
		LOGI("Display", "Queued go-back (history depth: %zu)", screenHistoryCount);
		return true;
}

void DisplayManager::handleSleepScreenRedirect() {
		if (!currentScreen) return;
		const char* target = currentScreen->wakeScreenId();
		if (!target) return;
		const char* current = getCurrentScreenId();
		if (current && strcmp(current, target) == 0) return;
		skipHistoryPush = true;
		showScreen(target);
		LOGI("Display", "Sleep redirect: %s -> %s", current ? current : "?", target);
}

const char* DisplayManager::getCurrentScreenId() {
		// Return ID of current screen (nullptr if splash or unknown)
		for (size_t i = 0; i < screenCount; i++) {
				if (currentScreen == availableScreens[i].instance) {
						return availableScreens[i].id;
				}
		}
		return nullptr;  // Splash or unknown screen
}

const ScreenInfo* DisplayManager::getAvailableScreens(size_t* count) {
		if (count) *count = screenCount;
		return availableScreens;
}

#endif // HAS_DISPLAY
