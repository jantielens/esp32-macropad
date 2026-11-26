#include <cassert>
#include <cstring>
#include <string>
#include <vector>
#include <SD_MMC.h>

FakeFontStorage SD_MMC;
FakeFontStorage LittleFS;
static bool ota_active = false;
static bool active = true;
static unsigned writers = 0;
static unsigned delays = 0;
bool ota_activity_is_active() { return ota_active; }
bool ota_activity_try_begin_config_write() {
    if (ota_active) return false;
    ++writers;
    return true;
}
void ota_activity_finish_config_write() { assert(writers); --writers; }
void vTaskDelay(unsigned ticks) {
    assert(ticks == 1 && writers == 0);
    ++delays;
}

namespace epub_reader_impl {
bool current(uint32_t generation) { return active && generation == 1; }
}
#include "../src/app/widgets/epub_cache_storage.h"

int main() {
    constexpr char name[] = "chapter";
    constexpr char path[] = "/storage/.ereader-current";
    constexpr char temporary[] = "/storage/.ereader-current.tmp";
    uint8_t buffer[16]{};
    std::vector<uint8_t> original(64);
    for (size_t i = 0; i < original.size(); ++i) original[i] = static_cast<uint8_t>(i);
    {
        epub_reader_impl::CacheStorageAdapter cache(1, buffer, sizeof(buffer));
        assert(cache.beginWrite(name));
        assert(cache.write(original.data(), original.size()));
        assert(cache.endWrite());
        assert(!writers);
        assert(delays > 0);
    }
    {
        epub_reader_impl::CacheStorageAdapter cache(1, buffer, sizeof(buffer));
        uint8_t data[8]{};
        assert(cache.readAt(name, 0, data, sizeof(data)) == 8);
        assert(cache.readAt(name, 8, data, sizeof(data)) == 8);
        assert(SD_MMC.open_calls[path] == 1);
        assert(cache.readAt(name, 15, data, sizeof(data)) == 1 && data[0] == 15);
        assert(cache.readAt(name, 16, data, sizeof(data)) == 8 && data[0] == 16);
        assert(cache.readAt("another-chapter", 0, data, sizeof(data)) == 8);
        ota_active = true;
        assert(cache.readAt(name, 16, data, sizeof(data)) < 0);
        ota_active = false;
        assert(cache.beginWrite(name));
        const uint8_t replacement[] = {99, 98, 97};
        assert(cache.write(replacement, sizeof(replacement)));
        assert(cache.endWrite());
        assert(cache.readAt(name, 0, data, sizeof(data)) == 3 && data[0] == 99);
        assert(cache.beginWrite(name));
        assert(cache.write(original.data(), original.size()));
        ota_active = true;
        cache.abort();
        assert(SD_MMC.exists(temporary));
        ota_active = false;
        assert(SD_MMC.files[path] == std::vector<uint8_t>(replacement, replacement + sizeof(replacement)));
        assert(cache.beginWrite(name));
        assert(!SD_MMC.exists(temporary) || SD_MMC.files[temporary].empty());
        SD_MMC.short_write = true;
        assert(!cache.write(original.data(), original.size()));
        SD_MMC.short_write = false;
        cache.abort();
        assert(SD_MMC.files[path] == std::vector<uint8_t>(replacement, replacement + sizeof(replacement)));
        assert(!writers);
    }
}
