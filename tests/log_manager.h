// ============================================================================
// Test stub: log_manager.h — shadows the real ESP32 log_manager.h
// ============================================================================
// Provides no-op log macros so binding_template.cpp compiles on the host.

#ifndef LOG_MANAGER_H
#define LOG_MANAGER_H

#include <stdint.h>

enum LogLevel : uint8_t {
    LOG_LEVEL_ERROR = 1,
    LOG_LEVEL_WARN  = 2,
    LOG_LEVEL_INFO  = 3,
    LOG_LEVEL_DEBUG = 4,
};

#define LOGE(module, format, ...) ((void)0)
#define LOGW(module, format, ...) ((void)0)
#define LOGI(module, format, ...) ((void)0)
#define LOGD(module, format, ...) ((void)0)

#endif // LOG_MANAGER_H
