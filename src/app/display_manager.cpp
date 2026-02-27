#include "board_config.h"

#if HAS_DISPLAY

#include "display_manager.h"
#include "log_manager.h"
#include "rtos_task_utils.h"

#include <esp_timer.h>

// Include selected display driver header.
// Driver implementations are compiled via src/app/display_drivers.cpp.
#if DISPLAY_DRIVER == DISPLAY_DRIVER_TFT_ESPI
#include "drivers/tft_espi_driver.h"
#elif DISPLAY_DRIVER == DISPLAY_DRIVER_ARDUINO_GFX
#include "drivers/arduino_gfx_driver.h"
#elif DISPLAY_DRIVER == DISPLAY_DRIVER_ARDUINO_GFX_ST77916
#include "drivers/arduino_gfx_st77916_driver.h"
#elif DISPLAY_DRIVER == DISPLAY_DRIVER_ST7701_RGB
#include "drivers/st7701_rgb_driver.h"
#elif DISPLAY_DRIVER == DISPLAY_DRIVER_ST7703_DSI
#include "drivers/st7703_dsi_driver.h"
#elif DISPLAY_DRIVER == DISPLAY_DRIVER_ST7701_DSI
#include "drivers/st7701_dsi_driver.h"
#endif

#include <SPI.h>

static portMUX_TYPE g_splash_status_mux = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE g_perf_mux = portMUX_INITIALIZER_UNLOCKED;

static DisplayPerfStats g_perf = {0, 0, 0};
static bool g_perf_ready = false;
static uint32_t g_perf_window_start_ms = 0;
static uint16_t g_perf_frames_in_window = 0;

// Global instance
DisplayManager* displayManager = nullptr;

DisplayManager::DisplayManager(DeviceConfig* cfg) 
		: driver(nullptr), display(nullptr), config(cfg), currentScreen(nullptr), previousScreen(nullptr), pendingScreen(nullptr), 
			infoScreen(cfg, this), testScreen(this), fpsScreen(this),
							lvglTaskHandle(nullptr), lvglTaskAlloc{}, lvglMutex(nullptr),
						presentTaskHandle(nullptr), presentTaskAlloc{}, presentSem(nullptr), sharedLvTimerUs(0),
						screenCount(0), buf(nullptr), buf2(nullptr), flushPending(false), pendingSplashStatusSet(false) {
				pendingSplashStatus[0] = '\0';
		// Instantiate selected display driver
		#if DISPLAY_DRIVER == DISPLAY_DRIVER_TFT_ESPI
		driver = new TFT_eSPI_Driver();
		#elif DISPLAY_DRIVER == DISPLAY_DRIVER_ARDUINO_GFX
		driver = new Arduino_GFX_Driver();
		#elif DISPLAY_DRIVER == DISPLAY_DRIVER_ARDUINO_GFX_ST77916
		driver = new Arduino_GFX_ST77916_Driver();
		#elif DISPLAY_DRIVER == DISPLAY_DRIVER_ST7701_RGB
		driver = new ST7701_RGB_Driver();
		#elif DISPLAY_DRIVER == DISPLAY_DRIVER_ST7703_DSI
		driver = new ST7703_DSI_Driver();
		#elif DISPLAY_DRIVER == DISPLAY_DRIVER_ST7701_DSI
		driver = new ST7701_DSI_Driver();
		#else
		#error "No display driver selected or unknown driver type"
		#endif
		
		// Create mutex for thread-safe LVGL access
		lvglMutex = xSemaphoreCreateMutex();
		
		// Initialize screen registry (exclude splash - it's boot-specific)
		availableScreens[0] = {"info", "Info Screen", &infoScreen};
		availableScreens[1] = {"test", "Display Test", &testScreen};
		availableScreens[2] = {"fps", "FPS Benchmark", &fpsScreen};
		screenCount = 3;
		#if HAS_TOUCH && LV_USE_CANVAS
		availableScreens[screenCount++] = {"touch_test", "Touch Test", &touchTestScreen};
		#endif
}

DisplayManager::~DisplayManager() {
		// Stop present task first (depends on driver, must be deleted before LVGL task)
		if (presentTaskHandle) {
				vTaskDelete(presentTaskHandle);
				presentTaskHandle = nullptr;
		}
		if (presentSem) {
				vSemaphoreDelete(presentSem);
				presentSem = nullptr;
		}
		
		// Stop rendering task
		if (lvglTaskHandle) {
				vTaskDelete(lvglTaskHandle);
				lvglTaskHandle = nullptr;
		}
		
		if (currentScreen) {
				currentScreen->hide();
		}
		
		splashScreen.destroy();
		infoScreen.destroy();
		testScreen.destroy();
		fpsScreen.destroy();
		#if HAS_TOUCH && LV_USE_CANVAS
		touchTestScreen.destroy();
		#endif
		
		// Delete display driver
		if (driver) {
				delete driver;
				driver = nullptr;
		}
		
		// Delete mutex
		if (lvglMutex) {
				vSemaphoreDelete(lvglMutex);
				lvglMutex = nullptr;
		}
		
		// Free LVGL buffers
		if (buf) {
				heap_caps_free(buf);
				buf = nullptr;
		}
		if (buf2) {
				heap_caps_free(buf2);
				buf2 = nullptr;
		}
}

const char* DisplayManager::getScreenIdForInstance(const Screen* screen) const {
		if (!screen) return nullptr;

		// Splash is boot-specific and intentionally not part of availableScreens.
		if (screen == &splashScreen) {
				return "splash";
		}

		// Registered runtime screens.
		for (size_t i = 0; i < screenCount; i++) {
				if (availableScreens[i].instance == screen) {
						return availableScreens[i].id;
				}
		}

		return nullptr;
}

// LVGL v9 flush callback
void DisplayManager::flushCallback(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
		DisplayManager* mgr = (DisplayManager*)lv_display_get_user_data(disp);

		uint32_t w = (area->x2 - area->x1 + 1);
		uint32_t h = (area->y2 - area->y1 + 1);
		
		// v9 stride: rows in px_map may be padded for cache-line alignment.
		// Tell the driver so it can advance the source pointer correctly.
		uint32_t stride = lv_draw_buf_width_to_stride(w, lv_display_get_color_format(disp));
		mgr->driver->flushSrcStride = stride;
		
		// Push pixels to display via driver HAL.
		bool swap = (mgr->driver->renderMode() != DisplayDriver::RenderMode::Buffered);
		mgr->driver->startWrite();
		mgr->driver->setAddrWindow(area->x1, area->y1, w, h);
		mgr->driver->pushColors((uint16_t *)px_map, w * h, swap);
		mgr->driver->endWrite();

		// Signal that the driver may need a post-render present() step.
		if (mgr) {
				mgr->flushPending = true;
		}
		
		// For async drivers (DMA2D), flush_ready is called from the DMA
		// completion callback.  For sync drivers, signal it here.
		if (!mgr->driver->asyncFlush()) {
				lv_display_flush_ready(disp);
		}
}

bool DisplayManager::isInLvglTask() const {
		if (!lvglTaskHandle) return false;
		return xTaskGetCurrentTaskHandle() == lvglTaskHandle;
}

void DisplayManager::lockIfNeeded(bool& didLock) {
		if (isInLvglTask()) {
				didLock = false;
				return;
		}
		lock();
		didLock = true;
}

void DisplayManager::unlockIfNeeded(bool didLock) {
		if (didLock) {
				unlock();
		}
}

void DisplayManager::lock() {
		if (lvglMutex) {
				xSemaphoreTake(lvglMutex, portMAX_DELAY);
		}
}

void DisplayManager::unlock() {
		if (lvglMutex) {
				xSemaphoreGive(lvglMutex);
		}
}

bool DisplayManager::tryLock(uint32_t timeoutMs) {
		if (!lvglMutex) return false;
		return xSemaphoreTake(lvglMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

// FreeRTOS task for continuous LVGL rendering
void DisplayManager::lvglTask(void* pvParameter) {
		DisplayManager* mgr = (DisplayManager*)pvParameter;
		
		LOGI("Display", "LVGL render task start (core %d)", xPortGetCoreID());
		
		while (true) {
				mgr->lock();

				// Apply any deferred splash status update.
				if (mgr->pendingSplashStatusSet) {
						char text[sizeof(mgr->pendingSplashStatus)];
						bool has = false;
						portENTER_CRITICAL(&g_splash_status_mux);
						if (mgr->pendingSplashStatusSet) {
								strlcpy(text, mgr->pendingSplashStatus, sizeof(text));
								mgr->pendingSplashStatusSet = false;
								has = true;
						}
						portEXIT_CRITICAL(&g_splash_status_mux);
						if (has) {
								mgr->splashScreen.setStatus(text);
						}
				}
				
				// Process pending screen switch (deferred from external calls)
				if (mgr->pendingScreen) {
						Screen* target = mgr->pendingScreen;
						if (mgr->currentScreen) {
								mgr->currentScreen->hide();
						}

						mgr->previousScreen = mgr->currentScreen;
						mgr->currentScreen = target;
						mgr->currentScreen->show();
						mgr->pendingScreen = nullptr;

						// Reset LVGL input device state so leftover PRESSED from the
						// previous screen doesn't fire a phantom CLICKED on the new screen.
						lv_indev_reset(NULL, NULL);

						const char* screenId = mgr->getScreenIdForInstance(mgr->currentScreen);
						LOGI("Display", "Switched to %s", screenId ? screenId : "(unregistered)");
				}
				
				// Handle LVGL rendering (animations, timers, etc.)
				const uint64_t lv_start_us = esp_timer_get_time();
				uint32_t delayMs = lv_timer_handler();
				const uint32_t lv_timer_us = (uint32_t)(esp_timer_get_time() - lv_start_us);
				
				// Update current screen (data refresh)
				if (mgr->currentScreen) {
						mgr->currentScreen->update();
				}
				
				// Flush canvas buffer only when LVGL produced draw data.
				if (mgr->flushPending) {
						if (mgr->driver->renderMode() == DisplayDriver::RenderMode::Buffered
								&& mgr->presentSem) {
								// Buffered mode: delegate present() to the async present task.
								// This frees the LVGL mutex during the slow QSPI panel transfer,
								// allowing touch input and animations to continue processing.
								mgr->sharedLvTimerUs = lv_timer_us;
								xSemaphoreGive(mgr->presentSem);
						} else {
								// Direct mode: present() is a no-op. Update perf stats inline.
								const uint32_t now_ms = millis();
								if (g_perf_window_start_ms == 0) {
										g_perf_window_start_ms = now_ms;
										g_perf_frames_in_window = 0;
								}

								g_perf_frames_in_window++;

								const uint32_t elapsed = now_ms - g_perf_window_start_ms;
								if (elapsed >= 1000) {
										const uint16_t fps = g_perf_frames_in_window;
										

										portENTER_CRITICAL(&g_perf_mux);
										g_perf.fps = fps;
										g_perf.lv_timer_us = lv_timer_us;
										g_perf.present_us = 0;
										g_perf_ready = true;
										portEXIT_CRITICAL(&g_perf_mux);

										g_perf_window_start_ms = now_ms;
										g_perf_frames_in_window = 0;
								}
						}
						mgr->flushPending = false;
				}
				
				mgr->unlock();
				
				// Sleep based on LVGL's suggested next timer deadline.
				// Clamp to keep UI responsive while avoiding busy looping on static screens.
				if (delayMs < 1) delayMs = 1;
				if (delayMs > 20) delayMs = 20;
				vTaskDelay(pdMS_TO_TICKS(delayMs));
		}
}

// FreeRTOS task: async QSPI panel transfer for Buffered render mode.
// Runs concurrently with the LVGL task — present() reads the PSRAM
// framebuffer while pushColors() may be writing to it.  The dirty-
// row spinlock in the driver ensures no tracking data is lost; pixel-
// level overlap is harmless (minor one-frame tear, self-correcting).
void DisplayManager::presentTask(void* pvParameter) {
		DisplayManager* mgr = (DisplayManager*)pvParameter;
		
		LOGI("Display", "Present task start (core %d)", xPortGetCoreID());
		
		while (true) {
				// Wait for signal from LVGL task
				xSemaphoreTake(mgr->presentSem, portMAX_DELAY);
				
				// Time the QSPI panel transfer
				const uint64_t start_us = esp_timer_get_time();
				mgr->driver->present();
				const uint32_t present_us = (uint32_t)(esp_timer_get_time() - start_us);
				
				// Update perf stats (frame count + periodic publish).
				// These statics are only accessed from one task context per board
				// (either here for Buffered, or inline in lvglTask for Direct).
				const uint32_t now_ms = millis();
				if (g_perf_window_start_ms == 0) {
						g_perf_window_start_ms = now_ms;
						g_perf_frames_in_window = 0;
				}
				g_perf_frames_in_window++;
				
				const uint32_t elapsed = now_ms - g_perf_window_start_ms;
				if (elapsed >= 1000) {
						const uint16_t fps = g_perf_frames_in_window;
						const uint32_t lv_us = mgr->sharedLvTimerUs;
						portENTER_CRITICAL(&g_perf_mux);
						g_perf.fps = fps;
						g_perf.lv_timer_us = lv_us;
						g_perf.present_us = present_us;
						g_perf_ready = true;
						portEXIT_CRITICAL(&g_perf_mux);
						
						g_perf_window_start_ms = now_ms;
						g_perf_frames_in_window = 0;
				}
		}
}

bool display_manager_get_perf_stats(DisplayPerfStats* out) {
		if (!out) return false;
		bool ok = false;
		portENTER_CRITICAL(&g_perf_mux);
		ok = g_perf_ready;
		if (ok) {
				*out = g_perf;
		}
		portEXIT_CRITICAL(&g_perf_mux);
		return ok;
}

void DisplayManager::initHardware() {
		LOGI("Display", "Init start");
		
		// Initialize display driver
		driver->init();
		driver->setRotation(DISPLAY_ROTATION);
		
		// Apply saved brightness from config (or default to 100%)
		#if HAS_BACKLIGHT
		uint8_t brightness = config ? config->backlight_brightness : 100;
		if (brightness > 100) brightness = 100;
		driver->setBacklightBrightness(brightness);
		LOGI("Display", "Backlight: %d%%", brightness);
		#else
		// Turn on backlight (on/off only)
		driver->setBacklight(true);
		LOGI("Display", "Backlight: ON");
		#endif
		
		LOGI("Display", "Resolution: %dx%d", DISPLAY_WIDTH, DISPLAY_HEIGHT);
		LOGI("Display", "Rotation: %d", DISPLAY_ROTATION);
		
		// Apply display-specific settings (inversion, gamma, etc.)
		driver->applyDisplayFixes();
		
		LOGI("Display", "Init complete");
}

void DisplayManager::initLVGL() {
		LOGI("Display", "LVGL v9 init start");
		
		lv_init();

		// Register tick callback (replaces v8 LV_TICK_CUSTOM macro)
		lv_tick_set_cb([]() -> uint32_t { return (uint32_t)millis(); });
		
		// Create display object (v9 API: resolution set at creation)
		display = lv_display_create(driver->width(), driver->height());
		if (!display) {
				LOGE("Display", "Failed to create lv_display");
				return;
		}
		
		// Set flush callback and user data
		lv_display_set_flush_cb(display, DisplayManager::flushCallback);
		lv_display_set_user_data(display, this);
		
		// Allocate LVGL draw buffer(s).
		// v9 uses raw byte buffers; size = pixels × bytes_per_pixel (2 for RGB565).
		// Buffers must be aligned to LV_DRAW_BUF_ALIGN (64 bytes on P4) for PPA DMA.
		const size_t buf_size_bytes = LVGL_BUFFER_SIZE * sizeof(uint16_t);
		const size_t buf_align = LV_DRAW_BUF_ALIGN;
		
		if (LVGL_BUFFER_PREFER_INTERNAL) {
				buf = (uint8_t*)heap_caps_aligned_alloc(buf_align, buf_size_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
				if (!buf) {
						LOGW("Display", "Internal RAM alloc failed, trying PSRAM...");
						buf = (uint8_t*)heap_caps_aligned_alloc(buf_align, buf_size_bytes, MALLOC_CAP_SPIRAM);
				}
		} else {
				buf = (uint8_t*)heap_caps_aligned_alloc(buf_align, buf_size_bytes, MALLOC_CAP_SPIRAM);
				if (!buf) {
						LOGW("Display", "PSRAM alloc failed, trying internal RAM...");
						buf = (uint8_t*)heap_caps_aligned_alloc(buf_align, buf_size_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
				}
		}
		if (!buf) {
				LOGE("Display", "Failed to allocate LVGL buffer");
				return;
		}
		LOGI("Display", "Buffer allocated: %d bytes (%d pixels, align=%d)", buf_size_bytes, LVGL_BUFFER_SIZE, buf_align);
		
		// Allocate second buffer for double-buffering if configured
		buf2 = NULL;
		#if defined(LVGL_DRAW_BUF_COUNT) && LVGL_DRAW_BUF_COUNT == 2
		if (LVGL_BUFFER_PREFER_INTERNAL) {
				buf2 = (uint8_t*)heap_caps_aligned_alloc(buf_align, buf_size_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
		} else {
				buf2 = (uint8_t*)heap_caps_aligned_alloc(buf_align, buf_size_bytes, MALLOC_CAP_SPIRAM);
		}
		if (buf2) {
				LOGI("Display", "Second buffer allocated for double-buffering: %d bytes", buf_size_bytes);
		} else {
				LOGW("Display", "Failed to allocate second buffer - using single-buffering");
		}
		#endif
		
		// Set buffers on display (v9 API)
		lv_display_set_buffers(display, buf, buf2, buf_size_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
		
		// Let driver set up hardware-specific LVGL configuration
		driver->configureLVGL(display, DISPLAY_ROTATION);
		
		// Initialize default theme (dark mode with custom primary color)
		// v9: lv_theme_default_init takes lv_display_t* (not NULL)
		lv_theme_t* theme = lv_theme_default_init(
				display,                        // Display
				lv_color_hex(0x3399FF),        // Primary color (light blue)
				lv_color_hex(0x303030),        // Secondary color (dark gray)
				true,                           // Dark mode
				LV_FONT_DEFAULT                // Default font
		);
		lv_display_set_theme(display, theme);
		LOGI("Display", "Theme: Default dark mode initialized");
		
		// Override LVGL refresh period if board specifies a custom value.
		#ifdef LVGL_REFR_PERIOD_MS
		lv_timer_set_period(lv_display_get_refr_timer(display), LVGL_REFR_PERIOD_MS);
		LOGI("Display", "Refresh period: %d ms", LVGL_REFR_PERIOD_MS);
		#endif
		
		LOGI("Display", "Buffer: %d pixels (%d lines), %s",
				 LVGL_BUFFER_SIZE, LVGL_BUFFER_SIZE / driver->width(),
				 #if defined(LVGL_DRAW_BUF_COUNT) && LVGL_DRAW_BUF_COUNT == 2
				 "double-buffered"
				 #else
				 "single-buffered"
				 #endif
		);
		LOGI("Display", "LVGL v9 init complete");
}

void DisplayManager::init() {
		// Initialize hardware (TFT + gamma fix)
		initHardware();
		
		// Initialize LVGL
		initLVGL();
		
		LOGI("Display", "Manager init start");
		
		// Create all screens
		splashScreen.create();
		infoScreen.create();
	testScreen.create();
		fpsScreen.create();
		#if HAS_TOUCH && LV_USE_CANVAS
		touchTestScreen.create();
		#endif

		// Show splash immediately
		showSplash();
		
		// Create LVGL rendering task
		// Stack size increased to 8KB for ESP32-S3 and larger displays
		// On dual-core: pin to configured core (LVGL_TASK_CORE)
		// On single-core: runs on Core 0 (time-sliced with Arduino loop)
		// Stack allocated in PSRAM when available to save internal RAM (~8 KB).
		#if CONFIG_FREERTOS_UNICORE
	if (!rtos_create_task_psram_stack(lvglTask, "LVGL", 8192, this, LVGL_TASK_PRIORITY, &lvglTaskHandle, &lvglTaskAlloc)) {
				xTaskCreate(lvglTask, "LVGL", 8192, this, LVGL_TASK_PRIORITY, &lvglTaskHandle);
				LOGI("Display", "Rendering task created (single-core, internal stack)");
		} else {
				LOGI("Display", "Rendering task created (single-core, PSRAM stack)");
		}
		#else
		if (!rtos_create_task_psram_stack_pinned(lvglTask, "LVGL", 8192, this, LVGL_TASK_PRIORITY, &lvglTaskHandle, &lvglTaskAlloc, LVGL_TASK_CORE)) {
				xTaskCreatePinnedToCore(lvglTask, "LVGL", 8192, this, LVGL_TASK_PRIORITY, &lvglTaskHandle, LVGL_TASK_CORE);
				LOGI("Display", "Rendering task created (Core %d, internal stack)", LVGL_TASK_CORE);
		} else {
				LOGI("Display", "Rendering task created (Core %d, PSRAM stack)", LVGL_TASK_CORE);
		}
		#endif
		
		// Create async present task for Buffered render mode.
		// Decouples the slow QSPI panel transfer from the LVGL timer/input loop,
		// allowing touch polling and animations to run at ~50 Hz instead of ~4 Hz.
		// On dual-core: pin to the OPPOSITE core from LVGL.  Both tasks are
		// priority 1, so sharing a core starves IDLE's WDT reset (the tasks
		// perfectly leapfrog and IDLE never runs).  Separate cores also give
		// true parallelism — the QSPI transfer runs while LVGL renders the
		// next frame.
		if (driver->renderMode() == DisplayDriver::RenderMode::Buffered) {
				presentSem = xSemaphoreCreateBinary();
				#if CONFIG_FREERTOS_UNICORE
				if (!rtos_create_task_psram_stack(presentTask, "Present", 4096, this, 1, &presentTaskHandle, &presentTaskAlloc)) {
						xTaskCreate(presentTask, "Present", 4096, this, 1, &presentTaskHandle);
						LOGI("Display", "Present task created (single-core, internal stack)");
				} else {
						LOGI("Display", "Present task created (single-core, PSRAM stack)");
				}
				#else
				const BaseType_t presentCore = 1 - LVGL_TASK_CORE;
				if (!rtos_create_task_psram_stack_pinned(presentTask, "Present", 4096, this, 1, &presentTaskHandle, &presentTaskAlloc, presentCore)) {
						xTaskCreatePinnedToCore(presentTask, "Present", 4096, this, 1, &presentTaskHandle, presentCore);
						LOGI("Display", "Present task created (Core %d, internal stack)", presentCore);
				} else {
						LOGI("Display", "Present task created (Core %d, PSRAM stack)", presentCore);
				}
				#endif
		}
		
		LOGI("Display", "Manager init complete");
}

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

int DisplayManager::getActiveWidth() const {
		if (display) return lv_display_get_horizontal_resolution(display);
		return driver ? driver->width() : 0;
}

int DisplayManager::getActiveHeight() const {
		if (display) return lv_display_get_vertical_resolution(display);
		return driver ? driver->height() : 0;
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
