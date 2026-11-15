#include "board_config.h"

#if HAS_RTC
#if RTC_DRIVER == RTC_DRIVER_PCF8563
#include "drivers/pcf8563_rtc_driver.cpp"
#else
#error "Unknown RTC_DRIVER."
#endif
#endif
