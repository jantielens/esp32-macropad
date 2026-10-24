#include "icon_store.h"
#include "pad_config.h"
#include "storage.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#define LODEPNG_NO_COMPILE_CPP
#include <libs/lodepng/lodepng.h>

FakeFontStorage LittleFS;
FakeFontStorage SD_MMC;
static PadConfig* configs[MAX_PADS] = {};
static bool present[MAX_PADS] = {};
static bool ota_active = false;

void log_noop(const char*, const char*, ...) {}
bool ota_activity_is_active() { return ota_active; }
uint32_t esp_random() { return 42; }
bool pad_config_exists(uint8_t page) { return present[page]; }
const PadConfig* pad_config_acquire(uint8_t page) { return configs[page]; }
void pad_config_release(const PadConfig*) {}
bool pad_icon_source_id_valid(const char* source) {
    return source && strlen(source) == 26 && strncmp(source, "s_", 2) == 0;
}
bool pad_icon_catalog_kind_valid(const char*, const char*) { return true; }
bool storage_write_atomic(const char*, const uint8_t*, size_t) { return false; }
bool storage_write_verified(const char* path, const uint8_t* bytes, size_t length) {
    LittleFS.files[path] = std::vector<uint8_t>(bytes, bytes + length);
    return true;
}

static void add_file(const char* path) {
    LittleFS.files[path] = {1, 2, 3};
}

int main(int argc, char**) {
    lv_init();
    icon_store_init();
    IconSourceUpload* upload = nullptr;
    assert(icon_store_source_png_begin(100, &upload) == ICON_STORE_UNAVAILABLE);
    const char* used_source = "s_000000000000000000000001";
    ScreenButtonConfig buttons[3] = {};
    for (unsigned index = 0; index < 3; ++index) {
        strcpy(buttons[index].icon_source_id, used_source);
        buttons[index].col = index;
    }
    PadConfig config = {};
    config.buttons = buttons;
    config.button_count = 3;
    config.main_button_count = 2;
    config.template_pad = 1;
    configs[0] = &config;
    present[0] = true;
    PadConfig parent = {};
    parent.template_pad = -1;
    configs[1] = &parent;
    present[1] = true;
    add_file("/icons/pad_0_main_0_0.png");
    add_file("/icons/pad_0_main_0_0.meta");
    add_file("/icons/pad_0_main_1_0.png");
    add_file("/icons/pad_0_overlay_2_0.png");
    add_file("/icons/pad_0_main_3_0.png");
    add_file("/icons/pad_0_main_3_0.meta");
    add_file("/icons/pad_2_main_0_0.png");
    add_file("/icons/pad_2_main_0_0.meta");
    add_file("/icon_sources/s_000000000000000000000001.png");
    add_file("/icon_sources/s_000000000000000000000002.png");

    if (argc > 1) {
        configs[1] = nullptr;
        icon_store_preload_pad_pages();
        assert(LittleFS.exists("/icons/pad_0_main_3_0.png"));
        assert(LittleFS.exists("/icons/pad_2_main_0_0.meta"));
        assert(LittleFS.exists("/icon_sources/s_000000000000000000000002.png"));
        icon_store_cleanup_pad_icons(0);
        assert(LittleFS.exists("/icons/pad_0_main_3_0.png"));
        puts("icon_store_cleanup unreadable: PASS");
        return 0;
    }

    icon_store_preload_pad_pages();
    assert(LittleFS.exists("/icons/pad_0_main_0_0.png"));
    assert(LittleFS.exists("/icons/pad_0_main_0_0.meta"));
    assert(LittleFS.exists("/icons/pad_0_main_1_0.png"));
    assert(LittleFS.exists("/icons/pad_0_overlay_2_0.png"));
    assert(!LittleFS.exists("/icons/pad_0_main_3_0.png"));
    assert(!LittleFS.exists("/icons/pad_0_main_3_0.meta"));
    assert(!LittleFS.exists("/icons/pad_2_main_0_0.png"));
    assert(!LittleFS.exists("/icons/pad_2_main_0_0.meta"));
    assert(LittleFS.exists("/icon_sources/s_000000000000000000000001.png"));
    assert(!LittleFS.exists("/icon_sources/s_000000000000000000000002.png"));

    add_file("/icons/pad_0_main_3_0.meta");
    add_file("/icons/pad_2_main_0_0.png");
    ota_active = true;
    icon_store_cleanup_pad_icons(0);
    assert(LittleFS.exists("/icons/pad_0_main_3_0.meta"));
    ota_active = false;
    LittleFS.remove_failure = true;
    icon_store_cleanup_pad_icons(0);
    assert(LittleFS.exists("/icons/pad_0_main_3_0.meta"));
    LittleFS.remove_failure = false;
    icon_store_cleanup_pad_icons(0);
    assert(!LittleFS.exists("/icons/pad_0_main_3_0.meta"));
    assert(LittleFS.exists("/icons/pad_2_main_0_0.png"));

    const uint8_t pixel[] = {255, 0, 0, 255};
    uint8_t* png = nullptr;
    size_t png_size = 0;
    assert(lodepng_encode32(&png, &png_size, pixel, 1, 1) == 0);
    IconSourceUpload* ready_upload = nullptr;
    assert(icon_store_source_png_begin(png_size, &ready_upload) == ICON_STORE_OK);
    icon_store_source_png_abort(ready_upload);
    assert(icon_store_install("pad_0_main_0_0", ICON_KIND_COLOR, used_source, 1,
                              png, png_size) == ICON_STORE_OK);
    lv_free(png);
    IconRef cached = {};
    assert(icon_store_lookup("pad_0_main_0_0", &cached));
    const lv_draw_buf_t* retired = reinterpret_cast<const lv_draw_buf_t*>(cached.dsc);
    buttons[0].icon_source_id[0] = '\0';
    icon_store_cleanup_pad_icons(0);
    assert(!icon_store_lookup("pad_0_main_0_0", &cached));
    assert(!LittleFS.exists("/icons/pad_0_main_0_0.png"));
    assert(retired->header.w == 1);
    icon_store_collect_retired(0);
    assert(!icon_store_source_delete_if_unreferenced(used_source));
    buttons[1].icon_source_id[0] = '\0';
    buttons[2].icon_source_id[0] = '\0';
    assert(icon_store_source_delete_if_unreferenced(used_source));

    for (unsigned index = 0; index < MAX_PAD_BUTTONS * 2 + 10; ++index) {
        char path[80];
        snprintf(path, sizeof(path), "/icons/pad_0_stale_%u.meta", index);
        add_file(path);
    }
    icon_store_delete_page_icons(0);
    for (const auto& file : LittleFS.files) assert(file.first.find("/icons/pad_0_") != 0);
    assert(LittleFS.exists("/icons/pad_2_main_0_0.png"));
    puts("icon_store_cleanup: PASS");
}