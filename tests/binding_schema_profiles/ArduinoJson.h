#pragma once

#include <stddef.h>

class JsonValue {
public:
    template <typename T>
    JsonValue& operator=(const T&) { return *this; }
};

class JsonDocument {
public:
    JsonValue operator[](const char*) { return {}; }
    void remove(const char*) {}
};

class DeserializationError {
public:
    explicit operator bool() const { return false; }
};

inline DeserializationError deserializeJson(JsonDocument&, const char*, size_t) { return {}; }

template <typename Output>
inline size_t serializeJson(const JsonDocument&, Output&) { return 0; }
