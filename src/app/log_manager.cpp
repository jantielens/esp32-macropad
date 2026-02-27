/*
 * Flat Logger Implementation
 *
 * Single-line, timestamped logs with no nesting/state.
 */

#include "log_manager.h"
#include <stdarg.h>

static bool g_log_manager_begun = false;

static inline bool serial_ready_for_logging() {
#if defined(ARDUINO_USB_CDC_ON_BOOT) && (ARDUINO_USB_CDC_ON_BOOT == 1)
		return (bool)Serial;
#else
		return g_log_manager_begun;
#endif
}

void log_init(unsigned long baud) {
		Serial.begin(baud);
		g_log_manager_begun = true;
}

static inline char log_level_char(LogLevel level) {
		switch (level) {
				case LOG_LEVEL_ERROR: return 'E';
				case LOG_LEVEL_WARN: return 'W';
				case LOG_LEVEL_INFO: return 'I';
				case LOG_LEVEL_DEBUG: return 'D';
				default: return 'I';
		}
}

void log_write(LogLevel level, const char* module, const char* format, ...) {
		if (!serial_ready_for_logging()) return;
		const unsigned long t = millis();

		char msgbuf[128];
		va_list args;
		va_start(args, format);
		vsnprintf(msgbuf, sizeof(msgbuf), format, args);
		va_end(args);

		char line[200];
		snprintf(line, sizeof(line), "[%lums] %c %s: %s\n", t, log_level_char(level), module, msgbuf);
		Serial.print(line);
}
