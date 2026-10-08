// Display Manager - Screen navigation, splash status, and screen history
#include "board_config.h"

#if HAS_DISPLAY

#include "display_manager.h"
#include "log_manager.h"
#include "pad_cycle.h"
#include "screen_saver_manager.h"

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
		LOGT("Display", "Queued switch to InfoScreen");
}

void DisplayManager::showTest() {
		// Defer screen switch to lvglTask (non-blocking)
		pendingScreen = &testScreen;
		LOGT("Display", "Queued switch to TestScreen");
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

void DisplayManager::prepareNavigationWake() {
		if (transientScreenActive) {
				pendingHistoryScreen = transientResumeScreen;
				transientResumeScreen = nullptr;
				transientScreenActive = false;
		}
		screen_saver_manager_notify_activity(true);
}

bool DisplayManager::showScreen(const char* screen_id, bool wake) {
		if (!screen_id) return false;
		bool didLock = false;
		lockIfNeeded(didLock);
		
		// Look up screen in registry
		for (size_t i = 0; i < screenCount; i++) {
				if (strcmp(availableScreens[i].id, screen_id) == 0) {
						// Defer screen switch to lvglTask (non-blocking)
						pendingScreen = availableScreens[i].instance;
						if (wake) {
								skipHistoryPush = false;
								prepareNavigationWake();
						}
						LOGT("Display", "Queued switch to screen: %s", screen_id);
						unlockIfNeeded(didLock);
						return true;
				}
		}
		
		unlockIfNeeded(didLock);
		LOGW("Display", "Screen not found: %s", screen_id);
		return false;
}

bool DisplayManager::showTransientScreen(const char* screen_id) {
		bool didLock = false;
		lockIfNeeded(didLock);
		if (!screen_id || !currentScreen || currentScreen == &splashScreen || pendingScreen) {
			unlockIfNeeded(didLock);
			return false;
		}

		for (size_t i = 0; i < screenCount; i++) {
			if (strcmp(availableScreens[i].id, screen_id) != 0) continue;
			if (!transientScreenActive) {
				transientResumeScreen = currentScreen;
				transientScreenActive = true;
			}
			skipHistoryPush = true;
			pendingScreen = availableScreens[i].instance;
			LOGT("Display", "Queued transient screen: %s", screen_id);
			unlockIfNeeded(didLock);
			return true;
		}

		unlockIfNeeded(didLock);
		LOGW("Display", "Transient screen not found: %s", screen_id);
		return false;
}

bool DisplayManager::restoreTransientScreen() {
		bool didLock = false;
		lockIfNeeded(didLock);
		if (!transientScreenActive) {
			unlockIfNeeded(didLock);
			return false;
		}

		Screen* target = transientResumeScreen;
		transientResumeScreen = nullptr;
		transientScreenActive = false;
		if (!target || target == currentScreen) {
			unlockIfNeeded(didLock);
			return true;
		}

		skipHistoryPush = true;
		pendingHistoryScreen = target;
		pendingScreen = target;
		LOGT("Display", "Queued transient screen restore");
		unlockIfNeeded(didLock);
		return true;
}

bool DisplayManager::goBack(bool wake) {
		bool didLock = false;
		lockIfNeeded(didLock);
		if (screenHistoryCount == 0) {
				unlockIfNeeded(didLock);
				return false;
		}
		pendingScreen = screenHistory[--screenHistoryCount];
		skipHistoryPush = true;
		if (wake) prepareNavigationWake();
		LOGT("Display", "Queued go-back (history depth: %zu)", screenHistoryCount);
		unlockIfNeeded(didLock);
		return true;
}

bool DisplayManager::cyclePad(int8_t direction, bool wrap, uint32_t excludedMask, bool wake) {
		bool didLock = false;
		lockIfNeeded(didLock);

		Screen* anchorScreen = pendingScreen ? pendingScreen : currentScreen;
		if (wake && transientScreenActive) anchorScreen = transientResumeScreen;
		int anchorPad = -1;
		for (uint8_t index = 0; index < MAX_PADS; index++) {
				if (anchorScreen == padScreens[index]) {
						anchorPad = index;
						break;
				}
		}

		int destination = pad_cycle_select(anchorPad, pad_config_get_eligible_mask(),
		                                     excludedMask, direction, wrap);
		if (destination >= 0) {
				pendingScreen = padScreens[destination];
				skipHistoryPush = false;
				if (wake) prepareNavigationWake();
		}
		unlockIfNeeded(didLock);

		if (destination >= 0) {
				LOGT("Display", "Queued cycle to Pad %d", destination + 1);
				return true;
		}
		return false;
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
