#include "loaded_data.h"

#if HAS_DISPLAY && HAS_PSRAM
#include "log_manager.h"
#include <esp_heap_caps.h>
#include <new>
#include <stdint.h>

LoadedDataSnapshot::LoadedDataSnapshot()
    : allocator_(SIZE_MAX), document_(&allocator_) {}

LoadedDataSnapshot* LoadedDataSnapshot::create() {
    void* memory = heap_caps_malloc(sizeof(LoadedDataSnapshot), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) {
        LOGE("LoadedData", "Cannot allocate snapshot in PSRAM");
        return nullptr;
    }
    return new (memory) LoadedDataSnapshot();
}

void LoadedDataSnapshot::retain() {
    references_.fetch_add(1, std::memory_order_relaxed);
}

void LoadedDataSnapshot::release() {
    if (references_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        this->~LoadedDataSnapshot();
        heap_caps_free(this);
    }
}

uint32_t LoadedDataSnapshot::references() const {
    return references_.load(std::memory_order_acquire);
}

uint64_t LoadedDataSlot::begin() {
    if (++request_ == 0) ++request_;
    if (current_) {
        current_->retired_next_ = retired_;
        retired_ = current_;
        current_ = nullptr;
    }
    state_ = LoadedDataState::Loading;
    error_ = nullptr;
    return request_;
}

bool LoadedDataSlot::current(uint64_t request) const {
    return request && request == request_ && state_ == LoadedDataState::Loading;
}

bool LoadedDataSlot::publish(uint64_t request, LoadedDataSnapshot* snapshot) {
    if (!snapshot || !current(request)) return false;
    current_ = snapshot;
    state_ = LoadedDataState::Ready;
    error_ = nullptr;
    return true;
}

bool LoadedDataSlot::fail(uint64_t request, const char* error) {
    if (!current(request)) return false;
    state_ = LoadedDataState::Error;
    error_ = error ? error : "load failed";
    return true;
}

LoadedDataSnapshot* LoadedDataSlot::acquire() const {
    if (state_ != LoadedDataState::Ready || !current_) return nullptr;
    current_->retain();
    return current_;
}

LoadedDataSnapshot* LoadedDataSlot::take_unused() {
    LoadedDataSnapshot** link = &retired_;
    while (*link) {
        LoadedDataSnapshot* snapshot = *link;
        if (snapshot->references() == 1) {
            *link = snapshot->retired_next_;
            snapshot->retired_next_ = nullptr;
            return snapshot;
        }
        link = &snapshot->retired_next_;
    }
    return nullptr;
}
#endif
