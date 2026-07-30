#pragma once

#include <stddef.h>

class String {
public:
    String(const char* value) : value_(value ? value : "") {}
    const char* c_str() const { return value_; }

private:
    const char* value_;
};