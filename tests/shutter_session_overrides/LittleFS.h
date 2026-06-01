// ============================================================================
// Test override: LittleFS.h — provides a FakeStorage class + global `LittleFS`
//
// The real src/app/storage.h does `#define Storage LittleFS`, so for shutter
// session tests we just need a `LittleFS` global that quacks like fs::FS for
// the methods session.cpp invokes (exists / mkdir / remove / rename / open).
//
// The File returned by open() is always invalid, which causes the persist
// task in shutter_session.cpp to bail out at its "Cannot open ... for
// writing" guard — exactly what we want for state-machine tests.
// ============================================================================
#pragma once

#include <Arduino.h>

class File {
public:
    explicit operator bool() const { return false; }
    size_t   write(const uint8_t*, size_t) { return 0; }
    size_t   write(uint8_t) { return 0; }
    size_t   print(const char*) { return 0; }
    size_t   print(const String&) { return 0; }
    int      read() { return -1; }
    size_t   size() const { return 0; }
    void     close() {}
};

class FakeStorage {
public:
    bool exists(const char*) { return false; }
    bool mkdir(const char*)  { return true;  }
    bool remove(const char*) { return true;  }
    bool rename(const char*, const char*) { return true; }
    File open(const char*, const char* = "r") { return File(); }
    File open(const String&, const char* = "r") { return File(); }
    uint64_t usedBytes()  const { return 0; }
    uint64_t totalBytes() const { return 0; }
};

extern FakeStorage LittleFS;
