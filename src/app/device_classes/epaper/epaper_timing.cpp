#include "epaper_timing.h"

#if HAS_EPAPER

#include <esp_attr.h>

// RTC-retained. Survives deep sleep; zeroed on cold boot.
RTC_DATA_ATTR EpaperTimingBudget epaper_timing_last = {};

#endif // HAS_EPAPER
