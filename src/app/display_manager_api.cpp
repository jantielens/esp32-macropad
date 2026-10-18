// Display Manager - C-API wrappers for app.ino
#include "board_config.h"

#if HAS_DISPLAY

#include "display_manager.h"
#if HAS_BOOT_LOGO
#include <esp_heap_caps.h>
#include <new>
#endif

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
		display_manager_show_screen(screen_id, success, false);
}

void display_manager_show_screen(const char* screen_id, bool* success, bool wake) {
		bool result = false;
		if (displayManager) {
				result = displayManager->showScreen(screen_id, wake);
		}
		if (success) *success = result;
}

bool display_manager_go_back() {
		return display_manager_go_back(false);
}

bool display_manager_go_back(bool wake) {
		if (displayManager) {
				return displayManager->goBack(wake);
		}
		return false;
}

bool display_manager_cycle_pad(int8_t direction, bool wrap, uint32_t excluded_mask) {
		return display_manager_cycle_pad(direction, wrap, excluded_mask, false);
}

bool display_manager_cycle_pad(int8_t direction, bool wrap, uint32_t excluded_mask, bool wake) {
		return displayManager
				? displayManager->cyclePad(direction, wrap, excluded_mask, wake)
				: false;
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

uint8_t display_manager_get_backlight_brightness() {
		if (displayManager && displayManager->getDriver()) {
				return displayManager->getDriver()->getBacklightBrightness();
		}
		return 100;
}

DisplayRefreshRequestResult display_manager_request_full_refresh() {
		if (!displayManager || !displayManager->getDriver() || !displayManager->getDriver()->isAvailable()) {
				return DISPLAY_REFRESH_UNAVAILABLE;
		}
		return displayManager->requestFullRefresh() ? DISPLAY_REFRESH_QUEUED : DISPLAY_REFRESH_FAILED;
}

int display_manager_get_presentation_mode() {
		return displayManager && displayManager->getDriver()
				? displayManager->getDriver()->presentationMode() : -1;
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

DisplayTaskDispatchResult display_manager_dispatch(
		DisplayTaskExec exec, DisplayTaskCleanup cleanup,
		const void* ctx, size_t ctx_len,
		uint32_t timeout_ms, bool* out_ok, char* out_msg, size_t out_msg_len) {
		if (!displayManager) return DISPLAY_TASK_DISPATCH_UNAVAILABLE;
		return displayManager->dispatch(
				exec, cleanup, ctx, ctx_len, timeout_ms, out_ok, out_msg, out_msg_len);
}

#if HAS_BOOT_LOGO
struct SplashLogoHandoff {
		uint16_t* pixels;
		uint16_t width;
		uint16_t height;
		bool installed;
};

static void splash_logo_install_job(const void* context, bool* ok, char*, size_t) {
		auto* handoff = *static_cast<SplashLogoHandoff* const*>(context);
		*ok = displayManager && handoff &&
				displayManager->getSplash()->setCustomLogo(
						handoff->pixels, handoff->width, handoff->height);
		if (*ok) handoff->installed = true;
}

static void splash_logo_handoff_cleanup(const void* context) {
		auto* handoff = *static_cast<SplashLogoHandoff* const*>(context);
		if (!handoff) return;
		if (!handoff->installed && handoff->pixels) heap_caps_free(handoff->pixels);
		delete handoff;
}

bool display_manager_get_splash_logo_bounds(uint16_t* width, uint16_t* height) {
		if (!displayManager || !width || !height) return false;
		bool did_lock = false;
		displayManager->lockIfNeeded(did_lock);
		const bool ok = displayManager->getSplash()->getLogoBounds(width, height);
		displayManager->unlockIfNeeded(did_lock);
		return ok;
}

bool display_manager_set_splash_logo(uint16_t* pixels, uint16_t width, uint16_t height) {
		if (!pixels) return false;
		auto* handoff = new (std::nothrow) SplashLogoHandoff{pixels, width, height, false};
		if (!handoff) {
				heap_caps_free(pixels);
				return false;
		}
		if (!displayManager) {
				heap_caps_free(pixels);
				delete handoff;
				return false;
		}
		if (displayManager->isInLvglTask()) {
				const bool installed = displayManager->getSplash()->setCustomLogo(pixels, width, height);
				if (!installed) heap_caps_free(pixels);
				delete handoff;
				return installed;
		}

		bool installed = false;
		SplashLogoHandoff* context = handoff;
		const DisplayTaskDispatchResult result = display_manager_dispatch(
				splash_logo_install_job, splash_logo_handoff_cleanup,
				&context, sizeof(context), 1500, &installed, nullptr, 0);
		if (result == DISPLAY_TASK_DISPATCH_TIMEOUT) return false;
		if (result != DISPLAY_TASK_DISPATCH_OK) {
				splash_logo_handoff_cleanup(&context);
				return false;
		}
		if (!handoff->installed) {
				heap_caps_free(pixels);
				delete handoff;
				return false;
		}
		delete handoff;
		return true;
}
#endif

void display_manager_lock_if_needed(bool* did_lock) {
		bool d = false;
		if (displayManager) {
				displayManager->lockIfNeeded(d);
		}
		if (did_lock) *did_lock = d;
}

void display_manager_unlock_if_needed(bool did_lock) {
		if (displayManager) {
				displayManager->unlockIfNeeded(did_lock);
		}
}

#endif // HAS_DISPLAY
