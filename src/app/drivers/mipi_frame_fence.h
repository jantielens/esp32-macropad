#pragma once

#include <stdint.h>
#ifdef ESP_PLATFORM
#include <esp_attr.h>
#define MIPI_FENCE_IRAM IRAM_ATTR
#else
#define MIPI_FENCE_IRAM
#endif

// Access under the same task/ISR critical section.
class MipiFrameFence {
public:
    static constexpr int unknownCore = -1;

    void MIPI_FENCE_IRAM observeCore(int core) {
        if (core < 0 || (interruptCore >= 0 && interruptCore != core)) {
            interruptCore = -2;
        } else if (interruptCore == unknownCore) {
            interruptCore = core;
        }
    }

    void MIPI_FENCE_IRAM arm(int presentingCore = unknownCore) {
        remaining = presentingCore >= 0 && presentingCore == interruptCore ? 1 : 2;
    }

    bool MIPI_FENCE_IRAM complete() {
        return remaining && --remaining == 0;
    }

private:
    uint8_t remaining = 0;
    int interruptCore = unknownCore;
};

class MipiDirectFrame {
public:
    enum class Flush { Invalid, Pending, Present };

    void reset(const void* initialScanout) {
        scanout = initialScanout;
        pending = nullptr;
    }

    void* inactive(void* first, void* second) const {
        if (pending || !first || !second || first == second) return nullptr;
        if (scanout == first) return second;
        if (scanout == second) return first;
        return nullptr;
    }

    Flush accept(const void* data, const void* first, const void* second, bool last) {
        if (!data || (data != first && data != second) || data == scanout ||
            (pending && pending != data)) {
            return Flush::Invalid;
        }
        pending = data;
        return last ? Flush::Present : Flush::Pending;
    }

    void complete() {
        scanout = pending;
        pending = nullptr;
    }

    void cancel() {
        pending = nullptr;
    }

private:
    const void* scanout = nullptr;
    const void* pending = nullptr;
};

#undef MIPI_FENCE_IRAM
