#pragma once

#include <stddef.h>
#include <stdint.h>

class File {
public:
    explicit operator bool() const { return false; }
    bool isDirectory() const { return false; }
    File openNextFile() { return {}; }
    const char* name() const { return ""; }
    size_t size() const { return 0; }
    size_t readBytes(char*, size_t) { return 0; }
    void close() {}
    size_t print(const char*) { return 0; }
    size_t print(char) { return 0; }
    template <typename... Args>
    size_t printf(const char*, Args...) { return 0; }
};

class HostLittleFS {
public:
    File open(const char*, const char* = nullptr) { return {}; }
    bool exists(const char*) const { return true; }
    bool mkdir(const char*) { return true; }
    bool remove(const char*) { return true; }
};

extern HostLittleFS LittleFS;
