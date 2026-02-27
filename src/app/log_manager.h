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
#define LOG_LEVEL LOG_LEVEL_INFO
#endif

// Initialize Serial logging.
void log_init(unsigned long baud);

// Core logging function (printf-style).
void log_write(LogLevel level, const char* module, const char* format, ...);

// Convenience duration helper.
inline void log_duration(const char* module, const char* label, unsigned long start_ms) {
		const unsigned long elapsed = millis() - start_ms;
		log_write(LOG_LEVEL_INFO, module, "%s dur=%lums", label, elapsed);
}

#if LOG_LEVEL >= LOG_LEVEL_ERROR
#define LOGE(module, format, ...) log_write(LOG_LEVEL_ERROR, module, format, ##__VA_ARGS__)
#else
#define LOGE(module, format, ...) ((void)0)
#endif

#if LOG_LEVEL >= LOG_LEVEL_WARN
#define LOGW(module, format, ...) log_write(LOG_LEVEL_WARN, module, format, ##__VA_ARGS__)
#else
#define LOGW(module, format, ...) ((void)0)
#endif

#if LOG_LEVEL >= LOG_LEVEL_INFO
#define LOGI(module, format, ...) log_write(LOG_LEVEL_INFO, module, format, ##__VA_ARGS__)
#else
#define LOGI(module, format, ...) ((void)0)
#endif

#if LOG_LEVEL >= LOG_LEVEL_DEBUG
#define LOGD(module, format, ...) log_write(LOG_LEVEL_DEBUG, module, format, ##__VA_ARGS__)
#else
#define LOGD(module, format, ...) ((void)0)
#endif

#define LOG_DURATION(module, label, start_ms) log_duration(module, label, start_ms)

#endif // LOG_MANAGER_H
