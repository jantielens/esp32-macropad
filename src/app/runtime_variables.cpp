#include "runtime_variables.h"
#include "board_config.h"

#if HAS_DISPLAY || HAS_BUTTON

#if HAS_PSRAM
#include <esp_heap_caps.h>
#endif

#if defined(ARDUINO)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#else
#include <mutex>
#endif

#include <stdlib.h>
#include <string.h>

namespace {
struct RuntimeVariable {
    char name[RUNTIME_VARIABLE_NAME_CAPACITY];
    char value[RUNTIME_VARIABLE_VALUE_CAPACITY];
};

RuntimeVariable* s_variables = nullptr;

#if defined(ARDUINO)
StaticSemaphore_t s_mutex_storage;
SemaphoreHandle_t s_mutex = xSemaphoreCreateRecursiveMutexStatic(&s_mutex_storage);

class RuntimeLock {
public:
    explicit RuntimeLock(SemaphoreHandle_t mutex) : mutex_(mutex) {
        xSemaphoreTakeRecursive(mutex_, portMAX_DELAY);
    }
    ~RuntimeLock() { xSemaphoreGiveRecursive(mutex_); }

private:
    SemaphoreHandle_t mutex_;
};
#else
std::recursive_mutex s_mutex;

class RuntimeLock {
public:
    explicit RuntimeLock(std::recursive_mutex& mutex) : mutex_(mutex) { mutex_.lock(); }
    ~RuntimeLock() { mutex_.unlock(); }

private:
    std::recursive_mutex& mutex_;
};
#endif

bool value_length_valid(const char* value, size_t* length) {
    if (!value) return false;
    const size_t size = strnlen(value, RUNTIME_VARIABLE_VALUE_CAPACITY);
    if (size == RUNTIME_VARIABLE_VALUE_CAPACITY) return false;
    if (length) *length = size;
    return true;
}

int find_variable(const char* name) {
    if (!s_variables) return -1;
    for (size_t index = 0; index < RUNTIME_VARIABLE_MAX_COUNT; ++index) {
        if (s_variables[index].name[0] && strcmp(s_variables[index].name, name) == 0)
            return static_cast<int>(index);
    }
    return -1;
}
}

bool runtime_variable_name_is_valid(const char* name) {
    if (!name) return false;
    const size_t length = strnlen(name, RUNTIME_VARIABLE_NAME_CAPACITY);
    if (length == 0 || length >= RUNTIME_VARIABLE_NAME_CAPACITY) return false;
    for (size_t index = 0; index < length; ++index) {
        const unsigned char c = static_cast<unsigned char>(name[index]);
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
    }
    return true;
}

RuntimeVariableLookupResult runtime_variable_get(const char* name, char* out,
                                                  size_t out_capacity) {
    if (!runtime_variable_name_is_valid(name)) return RUNTIME_VARIABLE_INVALID_NAME;
    if (!out || out_capacity == 0) return RUNTIME_VARIABLE_OUTPUT_TOO_SMALL;

    RuntimeLock lock(s_mutex);
    const int index = find_variable(name);
    if (index < 0) {
        out[0] = '\0';
        return RUNTIME_VARIABLE_MISSING;
    }
    const size_t length = strlen(s_variables[index].value);
    if (length >= out_capacity) {
        out[0] = '\0';
        return RUNTIME_VARIABLE_OUTPUT_TOO_SMALL;
    }
    memcpy(out, s_variables[index].value, length + 1);
    return RUNTIME_VARIABLE_FOUND;
}

RuntimeVariableSetResult runtime_variable_set(const char* name, const char* value) {
    if (!runtime_variable_name_is_valid(name)) return RUNTIME_VARIABLE_SET_INVALID_NAME;
    size_t value_length = 0;
    if (!value_length_valid(value, &value_length)) return RUNTIME_VARIABLE_SET_VALUE_TOO_LONG;

    RuntimeLock lock(s_mutex);
    if (!s_variables) {
#if HAS_PSRAM
        s_variables = static_cast<RuntimeVariable*>(heap_caps_malloc(
            sizeof(RuntimeVariable) * RUNTIME_VARIABLE_MAX_COUNT,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (s_variables)
            memset(s_variables, 0, sizeof(RuntimeVariable) * RUNTIME_VARIABLE_MAX_COUNT);
#else
        s_variables = static_cast<RuntimeVariable*>(calloc(
            RUNTIME_VARIABLE_MAX_COUNT, sizeof(RuntimeVariable)));
#endif
        if (!s_variables) return RUNTIME_VARIABLE_SET_NO_MEMORY;
    }
    int index = find_variable(name);
    if (index >= 0) {
        if (strcmp(s_variables[index].value, value) == 0) return RUNTIME_VARIABLE_UNCHANGED;
    } else {
        for (size_t candidate = 0; candidate < RUNTIME_VARIABLE_MAX_COUNT; ++candidate) {
            if (!s_variables[candidate].name[0]) {
                index = static_cast<int>(candidate);
                break;
            }
        }
        if (index < 0) return RUNTIME_VARIABLE_SET_FULL;
        memcpy(s_variables[index].name, name, strlen(name) + 1);
    }
    memcpy(s_variables[index].value, value, value_length + 1);
    return RUNTIME_VARIABLE_STORED;
}

void runtime_variable_transaction_lock() {
#if defined(ARDUINO)
    xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY);
#else
    s_mutex.lock();
#endif
}

void runtime_variable_transaction_unlock() {
#if defined(ARDUINO)
    xSemaphoreGiveRecursive(s_mutex);
#else
    s_mutex.unlock();
#endif
}

#endif // HAS_DISPLAY || HAS_BUTTON