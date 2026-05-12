#ifndef ARDUINO_GFX_PANEL_SLEEP_H
#define ARDUINO_GFX_PANEL_SLEEP_H

// Shared DCS sleep/wake helpers for Arduino_GFX-based display drivers.
// Both Arduino_GFX_Driver and Arduino_GFX_ST77916_Driver use these.

#include <Arduino_DataBus.h>

static inline void arduino_gfx_panel_sleep(Arduino_DataBus* bus) {
		if (!bus) return;
		bus->beginWrite();
		bus->writeCommand(0x28);  // Display Off
		bus->endWrite();
		bus->beginWrite();
		bus->writeCommand(0x10);  // Sleep In
		bus->endWrite();
}

// Phase 1: send Sleep Out only (DCS 0x11).
// Caller must wait ≥120 ms before calling arduino_gfx_panel_display_on().
static inline void arduino_gfx_panel_sleep_out(Arduino_DataBus* bus) {
		if (!bus) return;
		bus->beginWrite();
		bus->writeCommand(0x11);  // Sleep Out
		bus->endWrite();
}

// Phase 2: send Display On (DCS 0x29).
// Must be called ≥120 ms after arduino_gfx_panel_sleep_out().
static inline void arduino_gfx_panel_display_on(Arduino_DataBus* bus) {
		if (!bus) return;
		bus->beginWrite();
		bus->writeCommand(0x29);  // Display On
		bus->endWrite();
}

static inline void arduino_gfx_panel_wake(Arduino_DataBus* bus) {
		arduino_gfx_panel_sleep_out(bus);
		delay(120);               // DCS spec: 120 ms minimum after Sleep Out
		arduino_gfx_panel_display_on(bus);
}

#endif // ARDUINO_GFX_PANEL_SLEEP_H
