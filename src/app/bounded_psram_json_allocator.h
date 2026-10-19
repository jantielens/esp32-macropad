#pragma once

#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <stddef.h>

// Strict PSRAM budget for network documents; never falls back to internal RAM.
class BoundedPsramJsonAllocator : public ArduinoJson::Allocator {
    struct alignas(max_align_t) Header { size_t size; };
    size_t used_ = 0;
    const size_t limit_;
public:
    size_t peak = 0;
    explicit BoundedPsramJsonAllocator(size_t limit) : limit_(limit) {}
    void* allocate(size_t size) override {
        if (!size || size > limit_ - used_ || sizeof(Header) > limit_ - used_ - size) return nullptr;
        auto* p = static_cast<Header*>(heap_caps_malloc(size + sizeof(Header),
                                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!p) return nullptr;
        p->size = size + sizeof(Header);
        used_ += p->size;
        if (used_ > peak) peak = used_;
        return p + 1;
    }
    void deallocate(void* pointer) override {
        if (!pointer) return;
        auto* p = static_cast<Header*>(pointer) - 1;
        used_ -= p->size;
        heap_caps_free(p);
    }
    void* reallocate(void* pointer, size_t size) override {
        if (!pointer) return allocate(size);
        if (!size) { deallocate(pointer); return nullptr; }
        auto* p = static_cast<Header*>(pointer) - 1;
        const size_t available = limit_ - (used_ - p->size);
        if (size > available || sizeof(Header) > available - size) return nullptr;
        const size_t previous = p->size;
        auto* next = static_cast<Header*>(heap_caps_realloc(p, size + sizeof(Header),
                                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!next) return nullptr;
        next->size = size + sizeof(Header);
        used_ = used_ - previous + next->size;
        if (used_ > peak) peak = used_;
        return next + 1;
    }
};
