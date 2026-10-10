#pragma once
#include "board_config.h"
#include <lvgl.h>
#include <stdint.h>

class PadFontCache {
public:
    PadFontCache() = default;
    ~PadFontCache() { clear(); }
    PadFontCache(const PadFontCache&) = delete;
    PadFontCache& operator=(const PadFontCache&) = delete;
    const lv_font_t* resolve(const char* name, uint16_t size, const lv_font_t* fallback, float metrics = 0);
    void clear();
private:
#if HAS_TTF_FONTS
    struct Asset;
    struct Instance;
    Asset* assets = nullptr;
    Instance* instances = nullptr;
    bool allocation_warning_logged = false;
#endif
};