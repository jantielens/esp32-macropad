/*
 * Lightweight Logger (flat, single-line)
 *
 * Format: [<ms>] <LEVEL> <MODULE>: <message>
 * Designed for multi-task safety (no shared nesting state).
 */

#ifndef LOG_MANAGER_H
#define LOG_MANAGER_H

#include <Arduino.h>

enum LogLevel : uint8_t {
		LOG_LEVEL_ERROR = 1,
		LOG_LEVEL_WARN = 2,
		LOG_LEVEL_INFO = 3,
		LOG_LEVEL_DEBUG = 4,
};

#ifndef LOG_LEVEL
#define LOG_LEVEL 4
#endif

#ifndef LOG_DIAGNOSTICS
#define LOG_DIAGNOSTICS "*"
#endif

#ifndef LOG_SHUTTER_CSV
#define LOG_SHUTTER_CSV 1
#endif

// Initialize Serial logging.
void log_init(unsigned long baud);

// Core logging function (printf-style).
void log_write(LogLevel level, const char* module, const char* format, ...);
bool log_diagnostics_enabled(const char* module);
void log_serial_begin();
void log_serial_end();

// Convenience duration helper.
inline void log_duration(const char* module, const char* label, unsigned long start_ms) {
		const unsigned long elapsed = millis() - start_ms;
		log_write(LOG_LEVEL_INFO, module, "%s dur=%lums", label, elapsed);
}

#define LOG_AT(level, module, format, ...) do { \
	if (LOG_LEVEL >= (level)) log_write(level, module, format, ##__VA_ARGS__); \
} while (0)
#define LOGE(module, format, ...) LOG_AT(LOG_LEVEL_ERROR, module, format, ##__VA_ARGS__)
#define LOGW(module, format, ...) LOG_AT(LOG_LEVEL_WARN, module, format, ##__VA_ARGS__)
#define LOGI(module, format, ...) LOG_AT(LOG_LEVEL_INFO, module, format, ##__VA_ARGS__)
#define LOGD(module, format, ...) LOG_AT(LOG_LEVEL_DEBUG, module, format, ##__VA_ARGS__)
#define LOGT(module, format, ...) do { \
	if (LOG_LEVEL >= LOG_LEVEL_DEBUG && log_diagnostics_enabled(module)) \
		log_write(LOG_LEVEL_DEBUG, module, format, ##__VA_ARGS__); \
} while (0)

#define LOG_DURATION(module, label, start_ms) log_duration(module, label, start_ms)

#endif // LOG_MANAGER_H
