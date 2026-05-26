#pragma once

#include <stdint.h>

// Battery percentage (0-100) from a millivolt reading, rounded to nearest 5%.
//
// Linear mapping: 3000 mV = 0%, 4200 mV = 100%. Sufficient for V1; a proper
// non-linear LiPo curve can be slotted in later without changing the call site.
//
// Pure function, no Arduino / ESP-IDF dependencies, so it can be unit-tested
// directly on the host.
inline uint8_t epaper_battery_percent(uint16_t mv) {
		if (mv <= 3000) return 0;
		if (mv >= 4200) return 100;
		uint32_t pct = (uint32_t)(mv - 3000) * 100u / 1200u;
		// Round to nearest 5%.
		return (uint8_t)(((pct + 2u) / 5u) * 5u);
}
