#include "board_config.h"

#if HAS_DISPLAY

#include "data_stream.h"
#include "display_manager.h"
#include "log_manager.h"
#include "pad_config.h"
#include "rtos_task_utils.h"
#include "screen_saver_manager.h"

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

portMUX_TYPE g_splash_status_mux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE g_perf_mux = portMUX_INITIALIZER_UNLOCKED;

DisplayPerfStats g_perf = {0, 0, 0};
bool g_perf_ready = false;
uint32_t g_perf_window_start_ms = 0;
uint16_t g_perf_frames_in_window = 0;

// Global instance
DisplayManager* displayManager = nullptr;

DisplayManager::DisplayManager(DeviceConfig* cfg) 
		: driver(nullptr), display(nullptr), config(cfg), currentScreen(nullptr), pendingScreen(nullptr),
		screenHistoryCount(0), skipHistoryPush(false), 
			infoScreen(cfg, this), testScreen(this), fpsScreen(this),
			#if HAS_TOUCH && LV_USE_CANVAS
			touchTestScreen(this),
			#endif
			padScreens{
				PadScreen(0, this), PadScreen(1, this), PadScreen(2, this), PadScreen(3, this),
				PadScreen(4, this), PadScreen(5, this), PadScreen(6, this), PadScreen(7, this)
			},
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

		// Register pad screens (pad_0 through pad_7)
		for (uint8_t i = 0; i < MAX_PADS && screenCount < MAX_SCREENS; i++) {
				static const char* pad_ids[] = {"pad_0", "pad_1", "pad_2", "pad_3", "pad_4", "pad_5", "pad_6", "pad_7"};
				static const char* pad_names[] = {"Pad 1", "Pad 2", "Pad 3", "Pad 4", "Pad 5", "Pad 6", "Pad 7", "Pad 8"};
				availableScreens[screenCount++] = {pad_ids[i], pad_names[i], &padScreens[i]};
		}
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
		for (uint8_t i = 0; i < MAX_PADS; i++) {
				padScreens[i].destroy();
		}
		
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

#if HAS_MQTT
		data_stream_init();
#endif
		
		// Create all screens
		splashScreen.create();
		infoScreen.create();
	testScreen.create();
		fpsScreen.create();
		#if HAS_TOUCH && LV_USE_CANVAS
		touchTestScreen.create();
		#endif
		for (uint8_t i = 0; i < MAX_PADS; i++) {
				padScreens[i].create();
		}

		// Show splash immediately
		showSplash();
		
		// Create LVGL rendering task
		// Stack size: 16KB to accommodate image rendering (draw pipeline
		// for lv_image with RGB565 source needs ~10-12 KB with transforms).
		// On dual-core: pin to configured core (LVGL_TASK_CORE)
		// On single-core: runs on Core 0 (time-sliced with Arduino loop)
		// Stack allocated in PSRAM when available to save internal RAM.
		static constexpr uint32_t kLvglStack = 16384;
		#if CONFIG_FREERTOS_UNICORE
	if (!rtos_create_task_psram_stack(lvglTask, "LVGL", kLvglStack, this, LVGL_TASK_PRIORITY, &lvglTaskHandle, &lvglTaskAlloc)) {
				xTaskCreate(lvglTask, "LVGL", kLvglStack, this, LVGL_TASK_PRIORITY, &lvglTaskHandle);
				LOGI("Display", "Rendering task created (single-core, internal stack)");
		} else {
				LOGI("Display", "Rendering task created (single-core, PSRAM stack)");
		}
		#else
		if (!rtos_create_task_psram_stack_pinned(lvglTask, "LVGL", kLvglStack, this, LVGL_TASK_PRIORITY, &lvglTaskHandle, &lvglTaskAlloc, LVGL_TASK_CORE)) {
				xTaskCreatePinnedToCore(lvglTask, "LVGL", kLvglStack, this, LVGL_TASK_PRIORITY, &lvglTaskHandle, LVGL_TASK_CORE);
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

int DisplayManager::getActiveWidth() const {
		if (display) return lv_display_get_horizontal_resolution(display);
		return driver ? driver->width() : 0;
}

int DisplayManager::getActiveHeight() const {
		if (display) return lv_display_get_vertical_resolution(display);
		return driver ? driver->height() : 0;
}

#endif // HAS_DISPLAY
