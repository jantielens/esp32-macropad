#include "icon_store.h"
#include "pad_config.h"
#include "storage.h"
#include "widgets/widget_registry.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define LODEPNG_NO_COMPILE_CPP
#include <libs/lodepng/lodepng.h>

FakeFontStorage LittleFS;
FakeFontStorage SD_MMC;
static PadConfig* configs[MAX_PADS] = {};
static bool present[MAX_PADS] = {};
static bool ota_active = false;
static unsigned reads = 0;

void log_noop(const char*, const char*, ...) {}
void* heap_caps_malloc(size_t size, uint32_t) { return malloc(size); }
void heap_caps_free(void* ptr) { free(ptr); }
bool psramFound() { return true; }
bool ota_activity_is_active() { return ota_active; }
uint32_t esp_random() { return 42; }
bool pad_config_exists(uint8_t page) { return present[page]; }
const PadConfig* pad_config_acquire(uint8_t page) { return configs[page]; }
void pad_config_release(const PadConfig*) {}
const WidgetType* widget_find(const char* type) {
    static WidgetType selector{};
    selector.visitResources = [](const uint8_t* data, WidgetResourceVisitor visitor, void* context) {
        if (data[0]) visitor(WidgetResourceKind::ColorIcon, "widget_option_icon_5", 5,
                             reinterpret_cast<const char*>(data), context);
    };
    return type && !strcmp(type, "test_option") ? &selector : nullptr;
}
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
    const char* option_source = "s_000000000000000000000003";
    ScreenButtonConfig buttons[3] = {};
    for (unsigned index = 0; index < 3; ++index) {
        strcpy(buttons[index].icon_source_id, used_source);
        buttons[index].col = index;
    }
    strcpy(buttons[2].widget.type, "test_option");
    strcpy(reinterpret_cast<char*>(buttons[2].widget.data), option_source);
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
    add_file("/icons/pad_0_overlay_2_0_opt_5.png");
    add_file("/icons/pad_0_main_3_0.png");
    add_file("/icons/pad_0_main_3_0.meta");
    add_file("/icons/pad_2_main_0_0.png");
    add_file("/icons/pad_2_main_0_0.meta");
    add_file("/icon_sources/s_000000000000000000000001.png");
    add_file("/icon_sources/s_000000000000000000000002.png");
    add_file("/icon_sources/s_000000000000000000000003.png");
    assert(icon_store_key_valid("pad_0_overlay_2_0_opt_5"));
    assert(!icon_store_key_valid("pad_0_overlay_2_0_opt_0"));
    assert(!icon_store_key_valid("pad_0_overlay_2_0_opt_05"));
    assert(!icon_store_key_valid("pad_0_overlay_2_0_opt_6"));

    if (argc > 1) {
        configs[1] = nullptr;
        icon_store_preload_pad_pages();
        assert(LittleFS.exists("/icons/pad_0_main_3_0.png"));
        assert(LittleFS.exists("/icons/pad_2_main_0_0.meta"));
        assert(LittleFS.exists("/icon_sources/s_000000000000000000000002.png"));
        puts("icon_store_cleanup unreadable: PASS");
        return 0;
    }

    LittleFS.icon_directory_opens = 0;
    icon_store_preload_pad_pages();
    assert(LittleFS.icon_directory_opens == 5);
    assert(LittleFS.exists("/icons/pad_0_main_0_0.png"));
    assert(LittleFS.exists("/icons/pad_0_main_0_0.meta"));
    assert(LittleFS.exists("/icons/pad_0_main_1_0.png"));
    assert(LittleFS.exists("/icons/pad_0_overlay_2_0.png"));
    assert(LittleFS.exists("/icons/pad_0_overlay_2_0_opt_5.png"));
    assert(!LittleFS.exists("/icons/pad_0_main_3_0.png"));
    assert(!LittleFS.exists("/icons/pad_0_main_3_0.meta"));
    assert(!LittleFS.exists("/icons/pad_2_main_0_0.png"));
    assert(!LittleFS.exists("/icons/pad_2_main_0_0.meta"));
    assert(LittleFS.exists("/icon_sources/s_000000000000000000000001.png"));
    assert(!LittleFS.exists("/icon_sources/s_000000000000000000000002.png"));
    assert(LittleFS.exists("/icon_sources/s_000000000000000000000003.png"));

    add_file("/icons/pad_2_main_0_0.png");

    const uint8_t pixel[] = {255, 0, 0, 255};
    uint8_t* png = nullptr;
    size_t png_size = 0;
    assert(lodepng_encode32(&png, &png_size, pixel, 1, 1) == 0);
    IconSourceUpload* ready_upload = nullptr;
    assert(icon_store_source_png_begin(png_size, &ready_upload) == ICON_STORE_OK);
    icon_store_source_png_abort(ready_upload);
    assert(icon_store_install("pad_0_main_0_0", ICON_KIND_COLOR, used_source, 1,
                              png, png_size) == ICON_STORE_OK);
    LittleFS.read_hook = []() { ++reads; };
    assert(icon_store_validate_artifact("pad_0_main_0_0", nullptr, used_source, nullptr,
                                        "color", 1) == ICON_ARTIFACT_VALID);
    assert(reads == 0);
    assert(icon_store_validate_artifact("pad_0_main_1_0", "pad_0_main_0_0", used_source, nullptr,
                                        "color", 1) == ICON_ARTIFACT_VALID);
    assert(reads == 0);
    assert(icon_store_validate_artifact("pad_0_main_0_0", nullptr, used_source, nullptr,
                                        "mono", 1) == ICON_ARTIFACT_INVALID_SOURCE);
    assert(icon_store_validate_artifact("pad_0_main_0_0", nullptr, used_source, nullptr,
                                        "color", 2) == ICON_ARTIFACT_INVALID_PNG);
    assert(reads > 0);
    assert(icon_store_install("pad_0_main_0_0", ICON_KIND_COLOR, used_source, 2,
                              png, png_size) == ICON_STORE_OK);
    assert(icon_store_validate_artifact("pad_0_main_0_0", nullptr, used_source, nullptr,
                                        "color", 1) == ICON_ARTIFACT_INVALID_PNG);
    assert(icon_store_validate_artifact("pad_0_main_0_0", nullptr, used_source, nullptr,
                                        "color", 2) == ICON_ARTIFACT_VALID);
    IconArtifactInfo artifact = {};
    assert(icon_store_prepare_artifact("pad_0_main_0_0", &artifact));
    reads = 0;
    assert(icon_store_validate_artifact("pad_0_main_0_0", nullptr, used_source, nullptr,
                                        "color", 2) == ICON_ARTIFACT_VALID);
    assert(reads == 0);
    assert(icon_store_install("pad_0_main_3_0", ICON_KIND_COLOR, used_source, 3,
                              png, png_size) == ICON_STORE_OK);
    reads = 0;
    assert(icon_store_validate_artifact("pad_0_main_0_0", nullptr, used_source, nullptr,
                                        "color", 2) == ICON_ARTIFACT_VALID);
    assert(reads == 0);
    assert(icon_store_delete_file("pad_0_main_3_0.png"));
    assert(icon_store_validate_artifact("pad_0_main_3_0", nullptr, used_source, nullptr,
                                        "color", 3) == ICON_ARTIFACT_INVALID_PNG);
    assert(icon_store_validate_artifact("pad_0_main_0_0", nullptr, used_source, "noto:house",
                                        "color", 2) == ICON_ARTIFACT_INVALID_SOURCE);
    assert(icon_store_validate_artifact("pad_0_main_0_0", nullptr, used_source, "",
                                        "color", 2) == ICON_ARTIFACT_INVALID_SOURCE);
    const char* catalog_source = "s_000000000000000000000004";
    const char* catalog_json = R"({"catalog_id":"material-symbols:home","kind":"mono","media_type":"image/svg+xml","data":"<svg/>"})";
    LittleFS.files["/icon_sources/s_000000000000000000000004.json"] =
        std::vector<uint8_t>(catalog_json, catalog_json + strlen(catalog_json));
    assert(icon_store_install("pad_0_main_3_0", ICON_KIND_MONO, catalog_source, 3,
                              png, png_size) == ICON_STORE_OK);
    reads = 0;
    assert(icon_store_validate_artifact("pad_0_main_3_0", nullptr, catalog_source,
                                        "material-symbols:home", "mono", 3) == ICON_ARTIFACT_VALID);
    assert(reads == 0);
    assert(icon_store_validate_artifact("pad_0_main_3_0", nullptr, catalog_source,
                                        "material-symbols:other", "mono", 3) == ICON_ARTIFACT_INVALID_SOURCE);
    assert(icon_store_source_delete_if_unreferenced(catalog_source));
    assert(icon_store_validate_artifact("pad_0_main_3_0", nullptr, catalog_source,
                                        "material-symbols:home", "mono", 3) == ICON_ARTIFACT_INVALID_SOURCE);
    std::vector<std::string> many_keys;
    for (unsigned index = 0; index < 42; ++index) {
        char key[CONFIG_ICON_KEY_MAX_LEN];
        snprintf(key, sizeof(key), "pad_3_main_%u_%u", index % 8, index / 8);
        assert(icon_store_install(key, ICON_KIND_COLOR, used_source, 1, png, png_size) == ICON_STORE_OK);
        many_keys.emplace_back(key);
    }
    reads = 0;
    for (const auto& key : many_keys)
        assert(icon_store_validate_artifact(key.c_str(), nullptr, used_source, nullptr,
                                            "color", 1) == ICON_ARTIFACT_VALID);
    assert(reads == 0);
    LittleFS.read_hook = nullptr;
    lv_free(png);
    IconRef cached = {};
    assert(icon_store_lookup("pad_0_main_0_0", &cached));
    const lv_draw_buf_t* retired = reinterpret_cast<const lv_draw_buf_t*>(cached.dsc);
    buttons[0].icon_source_id[0] = '\0';
    icon_store_retire_unused_pad_icons(0);
    assert(!icon_store_lookup("pad_0_main_0_0", &cached));
    assert(LittleFS.exists("/icons/pad_0_main_0_0.png"));
    assert(retired->header.w == 1);
    icon_store_collect_retired(0);
    assert(!icon_store_source_delete_if_unreferenced(used_source));
    buttons[1].icon_source_id[0] = '\0';
    buttons[2].icon_source_id[0] = '\0';
    assert(icon_store_source_delete_if_unreferenced(used_source));
    assert(!icon_store_source_delete_if_unreferenced(option_source));
    buttons[2].widget.data[0] = '\0';
    assert(icon_store_source_delete_if_unreferenced(option_source));

    for (unsigned index = 0; index < MAX_PAD_BUTTONS * 2 + 10; ++index) {
        char path[80];
        snprintf(path, sizeof(path), "/icons/pad_0_stale_%u.meta", index);
        add_file(path);
    }
    ota_active = true;
    icon_store_delete_page_icons(0);
    assert(LittleFS.exists("/icons/pad_0_stale_0.meta"));
    ota_active = false;
    LittleFS.remove_failure = true;
    icon_store_delete_page_icons(0);
    assert(LittleFS.exists("/icons/pad_0_stale_0.meta"));
    LittleFS.remove_failure = false;
    icon_store_delete_page_icons(0);
    for (const auto& file : LittleFS.files) assert(file.first.find("/icons/pad_0_") != 0);
    assert(LittleFS.exists("/icons/pad_2_main_0_0.png"));
    puts("icon_store_cleanup: PASS");
}