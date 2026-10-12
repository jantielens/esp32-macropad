#pragma once
#include "board_config.h"
#include <lvgl.h>
#include <stdint.h>

class PadFontCache {
public:
    explicit PadFontCache(uint16_t limit = 0) : instance_limit(limit) {}
    ~PadFontCache() { clear(); }
    PadFontCache(const PadFontCache&) = delete;
    PadFontCache& operator=(const PadFontCache&) = delete;
    const lv_font_t* resolve(const char* name, uint16_t size, const lv_font_t* fallback, float metrics = 0);
    void clear();
private:
    uint16_t instance_limit;
#if HAS_TTF_FONTS
    struct Asset;
    struct Instance;
    Asset* assets = nullptr;
    Instance* instances = nullptr;
    bool allocation_warning_logged = false;
#endif
};

struct DeviceTypography {
    char font_name[32];
    uint16_t base_size;
};

const DeviceTypography& device_typography_get();
void device_typography_init(const char* font_name, uint16_t base_size);
PadFontCache& device_typography_fonts();