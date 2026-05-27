#include "epaper_carousel.h"

#if HAS_EPAPER

#include <esp_attr.h>

// Carousel index persists across deep sleep (RTC memory),
// resets to 0 on cold boot or power loss.
RTC_DATA_ATTR uint8_t g_epaper_carousel_index = 0;

uint8_t epaper_carousel_next_index(uint8_t current, uint8_t count, bool stay) {
		// Single entry or stay flag: return current
		if (count <= 1 || stay) {
				return current;
		}
		// Advance with wraparound
		return (current + 1) % count;
}

#endif // HAS_EPAPER
