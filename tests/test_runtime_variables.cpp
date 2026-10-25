#include "runtime_variables.h"
#include "board_config.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if HAS_PSRAM
#include <esp_heap_caps.h>

static bool allocation_fails = true;
static size_t allocation_calls = 0;
static uint32_t allocation_caps = 0;

void* heap_caps_malloc(size_t size, uint32_t caps) {
       ++allocation_calls;
       allocation_caps = caps;
       return allocation_fails ? nullptr : std::malloc(size);
}
#endif

static int failures = 0;

static void expect(bool condition, const char* label) {
    if (!condition) {
        std::printf("FAIL: %s\n", label);
        ++failures;
    }
}

int main() {
    char value[RUNTIME_VARIABLE_VALUE_CAPACITY] = {};
    expect(runtime_variable_get("missing", value, sizeof(value)) == RUNTIME_VARIABLE_MISSING,
           "unset variable is missing");
#if HAS_PSRAM
    expect(allocation_calls == 0, "reading an unset variable does not allocate");
    expect(runtime_variable_set("empty", "") == RUNTIME_VARIABLE_SET_NO_MEMORY &&
               runtime_variable_get("empty", value, sizeof(value)) == RUNTIME_VARIABLE_MISSING,
           "PSRAM allocation failure leaves the variable unset");
    expect(allocation_calls == 1 && allocation_caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
           "allocation requests PSRAM only without internal fallback");
    allocation_fails = false;
#endif
    expect(runtime_variable_set("empty", "") == RUNTIME_VARIABLE_STORED,
           "empty string can be stored");
    expect(runtime_variable_get("empty", value, sizeof(value)) == RUNTIME_VARIABLE_FOUND &&
               value[0] == '\0', "empty value differs from missing");

       expect(runtime_variable_set("empty", "") == RUNTIME_VARIABLE_UNCHANGED,
                 "unchanged assignment is a no-op");
    expect(runtime_variable_set("Empty", "case") == RUNTIME_VARIABLE_STORED &&
               runtime_variable_get("empty", value, sizeof(value)) == RUNTIME_VARIABLE_FOUND &&
               value[0] == '\0', "names are case-sensitive");

    char max_name[RUNTIME_VARIABLE_NAME_CAPACITY];
    memset(max_name, 'n', sizeof(max_name) - 1);
    max_name[sizeof(max_name) - 1] = '\0';
    expect(runtime_variable_name_is_valid(max_name), "32-character name is valid");
    expect(runtime_variable_set(max_name, "ok") == RUNTIME_VARIABLE_STORED,
           "32-character name can be assigned");
    expect(!runtime_variable_name_is_valid("bad/name") &&
               runtime_variable_set("bad/name", "x") == RUNTIME_VARIABLE_SET_INVALID_NAME,
           "invalid characters are rejected");
    max_name[sizeof(max_name) - 1] = 'n';
    expect(!runtime_variable_name_is_valid(max_name), "33-character name is rejected");

    char max_value[RUNTIME_VARIABLE_VALUE_CAPACITY];
    memset(max_value, 'v', sizeof(max_value) - 1);
    max_value[sizeof(max_value) - 1] = '\0';
    expect(runtime_variable_set("boundary", max_value) == RUNTIME_VARIABLE_STORED,
           "127-byte value is accepted");
    max_value[sizeof(max_value) - 1] = 'v';
       expect(runtime_variable_set("boundary", max_value) == RUNTIME_VARIABLE_SET_VALUE_TOO_LONG,
           "oversized value is rejected without mutation");
    expect(runtime_variable_get("boundary", value, sizeof(value)) == RUNTIME_VARIABLE_FOUND &&
               strlen(value) == RUNTIME_VARIABLE_VALUE_CAPACITY - 1,
           "failed oversized update preserves previous value");

       char name[16];
       size_t added = 0;
       for (size_t index = 0; index < RUNTIME_VARIABLE_MAX_COUNT; ++index) {
        std::snprintf(name, sizeof(name), "slot%02u", static_cast<unsigned>(index));
              const RuntimeVariableSetResult result = runtime_variable_set(name, "set");
              if (result == RUNTIME_VARIABLE_STORED) ++added;
              else if (result != RUNTIME_VARIABLE_SET_FULL) expect(false, "slot result is valid");
    }
       expect(added == RUNTIME_VARIABLE_MAX_COUNT - 4, "store accepts remaining capacity");
    expect(runtime_variable_set("slot00", "updated") == RUNTIME_VARIABLE_STORED,
           "existing variable updates while full");
    expect(runtime_variable_set("overflow", "rejected") == RUNTIME_VARIABLE_SET_FULL,
           "new variable rejected when full");
    expect(runtime_variable_get("slot00", value, sizeof(value)) == RUNTIME_VARIABLE_FOUND &&
               std::strcmp(value, "updated") == 0,
           "full-store rejection preserves existing namespace");
#if HAS_PSRAM
       expect(allocation_calls == 2, "successful storage is allocated once after failure recovery");
#endif

    if (failures) return 1;
    std::puts("runtime_variables: PASS");
    return 0;
}