/*
 * Display Driver Interface
 * 
 * Hardware abstraction layer for display libraries.
 * Allows DisplayManager to work with different display libraries
 * (TFT_eSPI, LovyanGFX, Adafruit_GFX, etc.) through a common interface.
 * 
 * IMPLEMENTATION GUIDE FOR NEW DRIVERS:
 * =====================================
 * 
 * 1. Create driver class implementing this interface:
 *    - drivers/your_driver.h (interface)
 *    - drivers/your_driver.cpp (implementation)
 * 
 * 2. Register implementation include in src/app/display_drivers.cpp:
 *    Arduino build system only compiles .cpp files in the sketch root directory.
 *    Driver implementations under src/app/drivers/ are compiled by including the
 *    selected driver .cpp from display_drivers.cpp.
 * 
 * 3. Add driver constant to board_config.h:
 *    #define DISPLAY_DRIVER_YOUR_DRIVER 3  // Next available number
 * 
 * 4. Configure in board override file:
 *    #define DISPLAY_DRIVER DISPLAY_DRIVER_YOUR_DRIVER
 *
 * 5. Choose render mode:
 *    - Direct: driver pushes pixels to panel in LVGL flush callback
 *    - Buffered: driver accumulates into a buffer and implements present()
 */

#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include <Arduino.h>
#include <lvgl.h>

// ============================================================================
// Display Driver Interface
// ============================================================================
// Pure virtual interface for display hardware abstraction.
// Minimal set of methods required for LVGL integration.

class DisplayDriver {
public:
		virtual ~DisplayDriver() = default;

		enum class RenderMode : uint8_t {
				// Driver pushes pixels to the panel during the LVGL flush callback.
				Direct = 0,
				// Driver accumulates LVGL flush data into an intermediate buffer/framebuffer.
				// DisplayManager must call present() to push that buffer to the panel.
				Buffered = 1,
		};
		
		// Hardware initialization
		virtual void init() = 0;
		
		// Display configuration
		virtual void setRotation(uint8_t rotation) = 0;

		// Active coordinate space dimensions for setAddrWindow/pushColors.
		// This is the resolution that direct pixel writes must target.
		// Drivers should report the post-rotation width/height of their address space.
		virtual int width() = 0;
		virtual int height() = 0;
		virtual void setBacklight(bool on) = 0;
		
		// Backlight brightness control (0-100%)
		virtual void setBacklightBrightness(uint8_t brightness) = 0;  // 0-100
		virtual uint8_t getBacklightBrightness() = 0;
		virtual bool hasBacklightControl() = 0;  // Capability query
		
		// Display-specific fixes/configuration (optional, board-dependent)
		virtual void applyDisplayFixes() = 0;
		
		// LVGL flush interface (critical path - called frequently)
		virtual void startWrite() = 0;
		virtual void endWrite() = 0;
		virtual void setAddrWindow(int16_t x, int16_t y, uint16_t w, uint16_t h) = 0;
		virtual void pushColors(uint16_t* data, uint32_t len, bool swap_bytes = true) = 0;

		// LVGL v9 stride: source pixel row stride in bytes.
		// Set by flush callback before pushColors() when LV_DRAW_BUF_STRIDE_ALIGN
		// causes padding beyond width * bpp.  0 = no extra padding (stride = width * bpp).
		uint32_t flushSrcStride = 0;

		// Declare whether the driver is Direct or Buffered.
		// Default: Direct (most SPI/QSPI drivers push pixels immediately in flush callback).
		virtual RenderMode renderMode() const {
				return RenderMode::Direct;
		}

		// For buffered drivers, push the accumulated framebuffer/canvas to the panel.
		// Default: no-op (Direct drivers do not need an explicit present).
		virtual void present() {
				// Override in buffered drivers (e.g., Arduino_GFX canvas)
		}

		// LVGL configuration hook (override to customize LVGL display settings)
		// Called during LVGL initialization to allow driver-specific configuration
		// such as software rotation, full refresh mode, etc.
		// Default implementation: no special configuration needed
		virtual void configureLVGL(lv_display_t* disp, uint8_t rotation) {
				// Default: hardware handles rotation via setRotation()
				// Override if driver needs software rotation or other LVGL tweaks
		}

		// Whether the driver signals flush-ready asynchronously (e.g., via DMA
		// completion callback). When true, DisplayManager skips calling
		// lv_display_flush_ready() in the flush callback.
		virtual bool asyncFlush() const { return false; }
};

#endif // DISPLAY_DRIVER_H
