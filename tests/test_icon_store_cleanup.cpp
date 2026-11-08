#include "icon_store.h"
#include "pad_config.h"
#include "storage.h"
#include "widgets/widget_registry.h"
#include <cassert>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#define LODEPNG_NO_COMPILE_CPP
#include <libs/lodepng/lodepng.h>

FakeFontStorage LittleFS;
FakeFontStorage SD_MMC;
static PadConfig* configs[MAX_PADS] = {};
static bool present[MAX_PADS] = {};
static bool ota_active = false;
static unsigned reads = 0;
static unsigned config_acquires = 0;
static unsigned config_exists_checks = 0;
static bool fail_heap_allocations = false;
static std::mutex mutation_mutex;
static std::mutex test_mutex;
static std::condition_variable test_condition;
static bool preload_read_started = false;
static bool writer_attempted = false;

extern "C" unsigned long millis() { return 0; }
void log_noop(const char*, const char*, ...) {}
void* heap_caps_malloc(size_t size, uint32_t) {
    return fail_heap_allocations ? nullptr : malloc(size);
}
void heap_caps_free(void* ptr) { free(ptr); }
bool psramFound() { return true; }
bool ota_activity_is_active() { return ota_active; }
uint32_t esp_random() { return 42; }
bool pad_config_exists(uint8_t page) { ++config_exists_checks; return present[page]; }
const PadConfig* pad_config_acquire(uint8_t page) { ++config_acquires; return configs[page]; }
void pad_config_release(const PadConfig*) {}
bool pad_config_lock_mutations() { mutation_mutex.lock(); return true; }
void pad_config_unlock_mutations() { mutation_mutex.unlock(); }
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

static void put_artifact(const char* key, const char* source, IconKind kind,
                         const uint8_t* png, size_t png_size, unsigned width = 1) {
    char path[80], metadata[256];
    snprintf(path, sizeof(path), "/icons/%s.png", key);
    LittleFS.files[path] = std::vector<uint8_t>(png, png + png_size);
    snprintf(path, sizeof(path), "/icons/%s.meta", key);
    snprintf(metadata, sizeof(metadata),
             "{\"source_id\":\"%s\",\"kind\":%u,\"width\":%u,\"height\":1,\"render_key\":1}",
             source, kind, width);
    LittleFS.files[path] = std::vector<uint8_t>(metadata, metadata + strlen(metadata));
}

int main(int argc, char**) {
    lv_init();
    LittleFS.mkdir("/icons");
    LittleFS.mkdir("/icon_sources");
    LittleFS.mkdir("/icons/folder.tmp");
    add_file("/icons/a-kept.png");
    add_file("/icons/unknown");
    LittleFS.unsupported_entries.insert("/icons/unknown");
    add_file("/icons/z-restored.png.bak");
    add_file("/icons/z-kept.png");
    add_file("/icons/z-kept.png.bak");
    add_file("/icon_sources/z-remove.json.tmp");
    add_file("/icon_sources/z-restored.json.bak");
    icon_store_init();
    assert(LittleFS.exists("/icons/folder.tmp"));
    assert(LittleFS.exists("/icons/unknown"));
    assert(LittleFS.exists("/icons/z-restored.png"));
    assert(!LittleFS.exists("/icons/z-restored.png.bak"));
    assert(LittleFS.exists("/icons/z-kept.png"));
    assert(!LittleFS.exists("/icons/z-kept.png.bak"));
    assert(!LittleFS.exists("/icon_sources/z-remove.json.tmp"));
    assert(LittleFS.exists("/icon_sources/z-restored.json"));
    assert(!LittleFS.exists("/icon_sources/z-restored.json.bak"));
    assert(LittleFS.open_calls["/icons/a-kept.png"] == 0);
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
    LittleFS.mkdir("/icon_sources/s_000000000000000000000006.png");
    add_file("/icon_sources/a-unknown");
    LittleFS.unsupported_entries.insert("/icon_sources/a-unknown");
    assert(icon_store_key_valid("pad_0_overlay_2_0_opt_5"));
    assert(!icon_store_key_valid("pad_0_overlay_2_0_opt_0"));
    assert(!icon_store_key_valid("pad_0_overlay_2_0_opt_05"));
    assert(!icon_store_key_valid("pad_0_overlay_2_0_opt_6"));

    const uint8_t pixel[] = {255, 0, 0, 255};
    uint8_t* png = nullptr;
    size_t png_size = 0;
    assert(lodepng_encode32(&png, &png_size, pixel, 1, 1) == 0);
    put_artifact("pad_0_main_0_0", used_source, ICON_KIND_COLOR, png, png_size);
    put_artifact("pad_0_main_1_0", option_source, ICON_KIND_COLOR, png, png_size);
    put_artifact("pad_1_main_1_0", used_source, ICON_KIND_COLOR, png, png_size);
    put_artifact("pad_0_overlay_2_0", used_source, ICON_KIND_COLOR, png, png_size, 2);
    put_artifact("pad_0_overlay_2_0_opt_5", option_source, ICON_KIND_COLOR, png, png_size);
    put_artifact("pad_0_main_3_0", used_source, ICON_KIND_COLOR, png, png_size);

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
    LittleFS.open_calls.clear();
    LittleFS.read_calls.clear();
    config_acquires = config_exists_checks = 0;
    ScreenButtonConfig replacement_buttons[4] = {};
    for (unsigned index = 0; index < 3; ++index) replacement_buttons[index] = buttons[index];
    strcpy(replacement_buttons[3].icon_source_id, used_source);
    replacement_buttons[3].col = 3;
    PadConfig replacement = config;
    replacement.buttons = replacement_buttons;
    replacement.button_count = 4;
    replacement.main_button_count = 4;
    bool writer_entered_during_preload = false;
    bool writer_saved = false;
    LittleFS.read_hook = [] {
        std::unique_lock<std::mutex> lock(test_mutex);
        if (preload_read_started) return;
        preload_read_started = true;
        test_condition.notify_one();
        test_condition.wait(lock, [] { return writer_attempted; });
    };
    std::thread writer([&] {
        {
            std::unique_lock<std::mutex> lock(test_mutex);
            test_condition.wait(lock, [] { return preload_read_started; });
        }
        const bool acquired = mutation_mutex.try_lock();
        if (acquired && LittleFS.exists("/icons/pad_0_main_3_0.png")) {
            configs[0] = &replacement;
            writer_saved = true;
        }
        {
            std::lock_guard<std::mutex> lock(test_mutex);
            writer_entered_during_preload = acquired;
            writer_attempted = true;
        }
        test_condition.notify_one();
        if (!acquired) {
            mutation_mutex.lock();
            if (LittleFS.exists("/icons/pad_0_main_3_0.png")) {
                configs[0] = &replacement;
                writer_saved = true;
            }
        }
        mutation_mutex.unlock();
    });
    icon_store_preload_pad_pages();
    writer.join();
    LittleFS.read_hook = nullptr;
    assert(!writer_entered_during_preload);
    assert(!writer_saved);
    assert(config_acquires == MAX_PADS);
    assert(config_exists_checks <= MAX_PADS);
    assert(LittleFS.read_calls["/icons/pad_0_main_0_0.png"] == 1);
    assert(LittleFS.read_calls["/icons/pad_0_main_0_0.meta"] == 1);
    assert(LittleFS.read_calls["/icons/pad_0_main_1_0.png"] == 0);
    assert(LittleFS.open_calls["/icons/pad_0_main_0_0.png"] == 1);
    assert(LittleFS.open_calls["/icons/pad_0_overlay_2_0_opt_5.png"] == 1);
    IconRef preloaded = {};
    assert(icon_store_lookup("pad_0_main_0_0", &preloaded));
    assert(icon_store_lookup("pad_0_main_1_0", &preloaded));
    assert(!icon_store_lookup("pad_0_overlay_2_0", &preloaded));
    assert(icon_store_lookup("pad_0_overlay_2_0_opt_5", &preloaded));
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
    assert(LittleFS.exists("/icon_sources/s_000000000000000000000006.png"));

    add_file("/icon_sources/s_000000000000000000000005.png");
    fail_heap_allocations = true;
    icon_store_cleanup_unreferenced_sources();
    fail_heap_allocations = false;
    assert(LittleFS.exists("/icon_sources/s_000000000000000000000005.png"));
    icon_store_cleanup_unreferenced_sources();
    assert(!LittleFS.exists("/icon_sources/s_000000000000000000000005.png"));

    put_artifact("pad_0_overlay_2_0", used_source, ICON_KIND_COLOR, png, png_size);
    LittleFS.short_read_path = "/icons/pad_0_overlay_2_0.png";
    icon_store_preload_pad(0);
    assert(!icon_store_lookup("pad_0_overlay_2_0", &preloaded));
    LittleFS.short_read_path.clear();
    LittleFS.files["/icons/pad_0_overlay_2_0.png"][24] ^= 0xff;
    icon_store_preload_pad(0);
    assert(!icon_store_lookup("pad_0_overlay_2_0", &preloaded));
    put_artifact("pad_0_overlay_2_0", used_source, ICON_KIND_COLOR, png, png_size);
    icon_store_preload_pad(0);
    assert(icon_store_lookup("pad_0_overlay_2_0", &preloaded));

    add_file("/icons/pad_2_main_0_0.png");

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
    const std::string catalog_json =
        R"({"catalog_id":"material-symbols:home","kind":"mono","media_type":"image/svg+xml","data":")" +
        std::string(1200, ' ') + R"(<svg/>"})";
    LittleFS.files["/icon_sources/s_000000000000000000000004.json"] =
        std::vector<uint8_t>(catalog_json.begin(), catalog_json.end());
    const ScreenButtonConfig saved_button = buttons[0];
    strcpy(buttons[0].icon_source_id, catalog_source);
    buttons[0].icon_kind = ICON_KIND_MONO;
    const ScreenButtonConfig saved_second = buttons[1];
    strcpy(buttons[1].icon_source_id, catalog_source);
    buttons[1].icon_kind = ICON_KIND_MONO;
    put_artifact("pad_0_main_0_0", catalog_source, ICON_KIND_MONO, png, png_size);
    put_artifact("pad_0_main_1_0", catalog_source, ICON_KIND_MONO, png, png_size);
    LittleFS.open_calls.clear();
    LittleFS.read_calls.clear();
    icon_store_preload_pad(0);
    assert(icon_store_lookup("pad_0_main_0_0", &preloaded) && preloaded.kind == ICON_KIND_MONO);
    assert(icon_store_lookup("pad_0_main_1_0", &preloaded) && preloaded.kind == ICON_KIND_MONO);
    assert(LittleFS.open_calls["/icon_sources/s_000000000000000000000004.json"] == 1);
    assert(LittleFS.read_calls["/icon_sources/s_000000000000000000000004.json"] == 3);
    assert(LittleFS.open_calls["/icons/pad_0_main_0_0.png"] == 1);
    assert(LittleFS.read_calls["/icons/pad_0_main_0_0.png"] == 1);
    assert(LittleFS.read_calls["/icons/pad_0_main_0_0.meta"] == 1);
    LittleFS.files["/icon_sources/s_000000000000000000000004.json"][0] = '!';
    buttons[0].icon_kind = ICON_KIND_COLOR;
    icon_store_preload_pad(0);
    assert(!icon_store_lookup("pad_0_main_0_0", &preloaded));
    buttons[0] = saved_button;
    buttons[1] = saved_second;
    assert(icon_store_install("pad_0_main_0_0", ICON_KIND_COLOR, used_source, 2,
                              png, png_size) == ICON_STORE_OK);
    LittleFS.files["/icon_sources/s_000000000000000000000004.json"] =
        std::vector<uint8_t>(catalog_json.begin(), catalog_json.end());
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