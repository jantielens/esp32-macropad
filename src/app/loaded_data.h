#pragma once

#include "board_config.h"

#if HAS_DISPLAY && HAS_PSRAM
#include "bounded_psram_json_allocator.h"
#include "display_manager_lock.h"
#include <atomic>
#include <stdint.h>

class LoadedDataLock {
public:
    LoadedDataLock() { display_manager_lock_if_needed(&locked_); }
    ~LoadedDataLock() { display_manager_unlock_if_needed(locked_); }
    LoadedDataLock(const LoadedDataLock&) = delete;
    LoadedDataLock& operator=(const LoadedDataLock&) = delete;
private:
    bool locked_ = false;
};

class LoadedDataSnapshot {
public:
    // Returns one owned reference, or nullptr if PSRAM allocation fails.
    static LoadedDataSnapshot* create();
    void retain();
    void release();
    uint32_t references() const;
    // Mutate only during private construction; published snapshots are immutable.
    JsonDocument& document() { return document_; }
    // Returned views borrow storage; retain a snapshot reference while using them.
    JsonVariantConst data() const { return document_.as<JsonVariantConst>(); }

    LoadedDataSnapshot(const LoadedDataSnapshot&) = delete;
    LoadedDataSnapshot& operator=(const LoadedDataSnapshot&) = delete;
private:
    LoadedDataSnapshot();
    ~LoadedDataSnapshot() = default;
    friend class LoadedDataSlot;
    std::atomic<uint32_t> references_{1};
    BoundedPsramJsonAllocator allocator_;
    JsonDocument document_;
    LoadedDataSnapshot* retired_next_ = nullptr;
};

enum class LoadedDataState : uint8_t { Idle, Loading, Ready, Error };

// Slot operations require LoadedDataLock. Slots are intended to live until reboot;
// destruction does not release snapshots automatically. Reclaim retired snapshots
// on the loop task, releasing them outside the lock.
class LoadedDataSlot {
public:
    // Starts a distinct request, retiring the previous snapshot immediately.
    uint64_t begin();
    // Cancel the current request and retire its snapshot.
    void clear();
    bool current(uint64_t request) const;
    // Success consumes the caller's reference; failure leaves ownership unchanged.
    bool publish(uint64_t request, LoadedDataSnapshot* snapshot);
    // On success, non-null error text is borrowed until begin(); use stable storage.
    bool fail(uint64_t request, const char* error);
    LoadedDataState state() const { return state_; }
    const char* error() const { return error_; }
    // Returns a retained reference, or nullptr unless ready. Caller must release().
    LoadedDataSnapshot* acquire() const;
    // Transfers one unused retired reference for release() on the loop, outside the lock.
    LoadedDataSnapshot* take_unused();
private:
    uint64_t request_ = 0;
    LoadedDataState state_ = LoadedDataState::Idle;
    const char* error_ = nullptr;
    LoadedDataSnapshot* current_ = nullptr;
    LoadedDataSnapshot* retired_ = nullptr;
};
#endif
