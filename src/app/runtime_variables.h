#pragma once

#include <stddef.h>
#include <stdint.h>

static constexpr size_t RUNTIME_VARIABLE_MAX_COUNT = 32;
static constexpr size_t RUNTIME_VARIABLE_NAME_CAPACITY = 33;
static constexpr size_t RUNTIME_VARIABLE_VALUE_CAPACITY = 128;

enum RuntimeVariableLookupResult {
    RUNTIME_VARIABLE_FOUND,
    RUNTIME_VARIABLE_MISSING,
    RUNTIME_VARIABLE_INVALID_NAME,
    RUNTIME_VARIABLE_OUTPUT_TOO_SMALL,
};

enum RuntimeVariableSetResult {
    RUNTIME_VARIABLE_STORED,
    RUNTIME_VARIABLE_UNCHANGED,
    RUNTIME_VARIABLE_SET_INVALID_NAME,
    RUNTIME_VARIABLE_SET_VALUE_TOO_LONG,
    RUNTIME_VARIABLE_SET_FULL,
    RUNTIME_VARIABLE_SET_NO_MEMORY,
};

bool runtime_variable_name_is_valid(const char* name);
RuntimeVariableLookupResult runtime_variable_get(const char* name, char* out,
                                                  size_t out_capacity);
RuntimeVariableSetResult runtime_variable_set(const char* name, const char* value);

// Lock order is display lock (when present), then the recursive variable mutex.
// Transactions and individual operations share it so resolution can read values
// without allowing a competing write between evaluation and assignment.
void runtime_variable_transaction_lock();
void runtime_variable_transaction_unlock();