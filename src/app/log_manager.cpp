/*
 * Flat Logger Implementation
 *
 * Single-line, timestamped logs with no nesting/state.
 */

#include "log_manager.h"
#include "board_config.h"
#include <stdarg.h>
#include <string.h>
#if defined(ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
static StaticSemaphore_t g_log_mutex_storage;
static SemaphoreHandle_t g_log_mutex = nullptr;
#endif

struct LogRepetition {
	uint32_t fingerprint;
	bool occupied;
	unsigned long last_ms;
	uint32_t suppressed;
};
static LogRepetition g_repetitions[32] = {};

static bool g_log_manager_begun = false;

static inline bool serial_ready_for_logging() {
#if defined(ARDUINO_USB_CDC_ON_BOOT) && (ARDUINO_USB_CDC_ON_BOOT == 1)
		return (bool)Serial;
#else
		return g_log_manager_begun;
#endif
}

void log_init(unsigned long baud) {
		#if defined(ESP32)
		if (!g_log_mutex) g_log_mutex = xSemaphoreCreateMutexStatic(&g_log_mutex_storage);
		#endif
		Serial.begin(baud);
		g_log_manager_begun = true;
}

void log_serial_begin() {
#if defined(ESP32)
	if (g_log_mutex) xSemaphoreTake(g_log_mutex, portMAX_DELAY);
#endif
}

void log_serial_end() {
#if defined(ESP32)
	if (g_log_mutex) xSemaphoreGive(g_log_mutex);
#endif
}

bool log_diagnostics_enabled(const char* module) {
	if (!module) return false;
	const char* selected = LOG_DIAGNOSTICS;
	const size_t length = strlen(module);
	while (*selected) {
		const char* end = strchr(selected, ',');
		const size_t token_length = end ? size_t(end - selected) : strlen(selected);
		if ((token_length == 1 && *selected == '*') ||
			(token_length == length && strncmp(selected, module, length) == 0)) return true;
		if (!end) break;
		selected = end + 1;
	}
	return false;
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
		if (level < LOG_LEVEL_ERROR || level > LOG_LEVEL_DEBUG || level > LOG_LEVEL ||
		    !module || !format || !serial_ready_for_logging()) return;

		char msgbuf[192];
		va_list args;
		va_start(args, format);
		const int required = vsnprintf(msgbuf, sizeof(msgbuf), format, args);
		va_end(args);
		if (required < 0) return;
		if (size_t(required) >= sizeof(msgbuf)) {
		    const char marker[] = "... [truncated]";
		    memcpy(msgbuf + sizeof(msgbuf) - sizeof(marker), marker, sizeof(marker));
		}
		for (char* cursor = msgbuf; *cursor; ++cursor) {
		    if (static_cast<unsigned char>(*cursor) < 32 || *cursor == 127) *cursor = ' ';
		}
		char safe_module[25];
		snprintf(safe_module, sizeof(safe_module), "%.24s", module);
		for (char* cursor = safe_module; *cursor; ++cursor) {
		    if (static_cast<unsigned char>(*cursor) < 32 || *cursor == 127) *cursor = ' ';
		}

		log_serial_begin();
		const unsigned long t = millis();
		uint32_t suppressed = 0;
		if (level <= LOG_LEVEL_WARN) {
		    uint32_t fingerprint = 2166136261U ^ uint32_t(level);
		    for (const unsigned char* cursor = reinterpret_cast<const unsigned char*>(module); *cursor; ++cursor)
		        fingerprint = (fingerprint ^ *cursor) * 16777619U;
		    fingerprint = (fingerprint ^ 0U) * 16777619U;
		    for (const unsigned char* cursor = reinterpret_cast<const unsigned char*>(msgbuf); *cursor; ++cursor)
		        fingerprint = (fingerprint ^ *cursor) * 16777619U;
		    LogRepetition* entry = nullptr;
		    for (auto& candidate : g_repetitions) {
		        if (candidate.occupied && candidate.fingerprint == fingerprint) {
		            entry = &candidate;
		            break;
		        }
		    }
		    if (entry && uint32_t(t - entry->last_ms) < 5000) {
		        ++entry->suppressed;
		        log_serial_end();
		        return;
		    }
		    if (!entry) {
		        entry = &g_repetitions[0];
		        for (auto& candidate : g_repetitions) {
		            if (!candidate.occupied) { entry = &candidate; break; }
		            if (uint32_t(t - candidate.last_ms) > uint32_t(t - entry->last_ms)) entry = &candidate;
		        }
		        *entry = {fingerprint, true, t, 0};
		    }
		    suppressed = entry->suppressed;
		    entry->suppressed = 0;
		    entry->last_ms = t;
		}

		char line[288];
		if (suppressed) {
		    snprintf(line, sizeof(line), "[%lums] %c %.24s: %s [suppressed=%lu]\n", t,
		             log_level_char(level), safe_module, msgbuf, (unsigned long)suppressed);
		} else {
		    snprintf(line, sizeof(line), "[%lums] %c %.24s: %s\n", t, log_level_char(level), safe_module, msgbuf);
		}
		Serial.print(line);
		log_serial_end();
}
