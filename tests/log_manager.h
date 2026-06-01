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

// Variadic template noop — evaluates all arguments (satisfying -Werror=unused-variable
// for variables used only in log calls) but emits no code at runtime.
template<typename... Args>
inline void log_noop(Args&&...) {}

#define LOGE(module, format, ...) log_noop(module, format, ##__VA_ARGS__)
#define LOGW(module, format, ...) log_noop(module, format, ##__VA_ARGS__)
#define LOGI(module, format, ...) log_noop(module, format, ##__VA_ARGS__)
#define LOGD(module, format, ...) log_noop(module, format, ##__VA_ARGS__)

#endif // LOG_MANAGER_H
