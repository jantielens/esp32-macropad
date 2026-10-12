#!/usr/bin/env python3
"""Exercise production cache allocation, failure, retry, and reuse paths."""
import pathlib
import re
import shutil
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]


def production(name):
    return re.sub(r"^\s*#include[^\n]*", "", (ROOT / "src/app" / name).read_text(), flags=re.M)


PRELUDE = r'''
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <ArduinoJson.h>
#include "board_config.h"
#undef HAS_MQTT
#define HAS_MQTT 1
#include "pad_config.h"
#include "action_list.h"
#include "list_provider.h"
#include "binding_template.h"
#include "mqtt_sub_store.h"
#include "psram_json_allocator.h"
#include "LittleFS.h"
#define Storage LittleFS
template <typename... Args> void state_test_log(Args...) {}
#define LOGI(...) state_test_log(__VA_ARGS__)
#define LOGD(...) state_test_log(__VA_ARGS__)
#define LOGT(...) state_test_log(__VA_ARGS__)
#define LOGW(...) state_test_log(__VA_ARGS__)
#define LOGE(...) state_test_log(__VA_ARGS__)

std::map<std::string, std::string> storage_test_files;
bool storage_test_fail_open = false;
size_t storage_test_write_limit = SIZE_MAX;
FakeLittleFS LittleFS;
static bool allocation_fail = true;
static unsigned allocations = 0;
void* config_psram_alloc(size_t size, const char*) {
    ++allocations;
    return allocation_fail ? nullptr : calloc(1, size);
}
static unsigned rebuilds = 0;
void pad_config_rebuild_all_caches() { ++rebuilds; }
void storage_publish_usage(bool) {}
bool pad_config_exists(uint8_t id) { return id == 0 || id == 2; }
char* pad_config_read_raw(uint8_t id, size_t* length) {
    const char* json = id == 0 ? "{\"name\":\"Home\"}" : "{}";
    *length = strlen(json);
    return strdup(json);
}
static const ListProvider* registered_provider = nullptr;
bool list_provider_register(const ListProvider* provider) {
    registered_provider = provider;
    return true;
}
bool binding_template_register(const char*, binding_resolver_fn, binding_topic_collector_fn,
                              const BindingSchemeSpec&) { return true; }
template <typename Value> Value constrain(Value value, Value low, Value high) {
    return std::max(low, std::min(value, high));
}
bool mqtt_sub_store_active() { return true; }
void mqtt_sub_store_collect_topic(void*, const char*) {}
bool mqtt_sub_store_get(const char*, char* output, size_t length, bool*, bool* truncated) {
    assert(length == MQTT_SUB_STORE_MAX_VALUE_LEN);
    std::string payload(1900, 'x');
    strlcpy(output, payload.c_str(), length);
    *truncated = false;
    return true;
}
bool mqtt_sub_store_extract_json(const char* payload, const char*, char* output, size_t length) {
    assert(strlen(payload) == 1900);
    strlcpy(output, "42", length);
    return true;
}
void mqtt_sub_store_format_value(const char* value, const char*, char* output, size_t length) {
    strlcpy(output, value, length);
}

struct lv_obj_t {};
struct lv_timer_t {};
struct lv_event_t {};
struct lv_font_t {};
static lv_font_t test_font;
const lv_font_t* pad_typography_font(int) { return &test_font; }
int pad_font_pixel_size(int) { return 24; }
int lv_font_get_line_height(const lv_font_t*) { return 24; }
const lv_font_t* lv_obj_get_style_text_font(lv_obj_t*, int) { return &test_font; }
struct DeviceTypography { const char* font_name; uint16_t base_size; };
const DeviceTypography& device_typography_get() {
    static const DeviceTypography active = {"@builtin", 0};
    return active;
}
bool font_store_validate_name(const char*) { return false; }
#ifndef HAS_TTF_FONTS
#define HAS_TTF_FONTS 0
#endif
using lv_color_t = int;
using lv_event_cb_t = void (*)(lv_event_t*);
static lv_obj_t top_layer;
lv_obj_t* lv_layer_top() { return &top_layer; }
lv_obj_t* lv_obj_create(lv_obj_t*) { return new lv_obj_t; }
lv_obj_t* lv_label_create(lv_obj_t* parent) { return parent; }
void lv_obj_delete(lv_obj_t* object) { delete object; }
lv_timer_t* lv_timer_create(void (*)(lv_timer_t*), uint32_t, void*) { return new lv_timer_t; }
void lv_timer_delete(lv_timer_t* timer) { delete timer; }
lv_color_t lv_color_white() { return 1; }
lv_color_t lv_color_black() { return 0; }
lv_color_t lv_color_hex(uint32_t color) { return color; }
#define LV_OPA_COVER 255
#define LV_OPA_60 153
#define LV_OBJ_FLAG_CLICKABLE 1
#define LV_EVENT_CLICKED 1
#define LV_ALIGN_TOP_MID 1
#define LV_ALIGN_BOTTOM_LEFT 2
#define LV_ALIGN_BOTTOM_RIGHT 3
#define LV_LABEL_LONG_WRAP 1
#define LV_LABEL_LONG_DOT 2
#define LV_PART_MAIN 0
#define LV_TEXT_ALIGN_CENTER 1
#define LV_PCT(value) (value)
#define NOOP(name) template <typename... Args> void name(Args...) {}
NOOP(lv_obj_remove_style_all)
NOOP(lv_obj_set_style_bg_color)
NOOP(lv_obj_set_style_bg_opa)
NOOP(lv_obj_set_style_radius)
NOOP(lv_obj_add_flag)
NOOP(lv_obj_add_event_cb)
NOOP(lv_label_set_text)
NOOP(lv_obj_set_style_text_color)
NOOP(lv_obj_set_style_text_font)
NOOP(lv_obj_center)
NOOP(lv_obj_set_size)
NOOP(lv_obj_set_style_border_color)
NOOP(lv_obj_set_style_border_width)
NOOP(lv_obj_set_style_outline_width)
NOOP(lv_obj_set_style_outline_color)
NOOP(lv_obj_align)
NOOP(lv_label_set_long_mode)
NOOP(lv_obj_set_width)
NOOP(lv_obj_set_height)
NOOP(lv_obj_set_style_text_align)
NOOP(lv_timer_set_repeat_count)
struct DisplayManager {
    int getActiveWidth() { return 466; }
    int getActiveHeight() { return 466; }
};
static DisplayManager manager;
DisplayManager* displayManager = &manager;
struct ScaleInfo { int font_medium; int font_large; };
ScaleInfo pad_get_scale_info() { return {0, 0}; }
static unsigned dispatched = 0;
void action_list_dispatch(const ButtonAction* actions, uint8_t count, const char*,
                          ActionContinuationOwner owner) {
    assert(owner == ACTION_CONTINUATION_OWNER_LVGL);
    assert(count == 1 && !strcmp(actions[0].type, "screen"));
    ++dispatched;
}
'''

CHECKS = r'''
int main() {
    allocator_probe::run_checks();
    ButtonAction action = {};
    strlcpy(action.type, "screen", sizeof(action.type));
    assert(!button_confirmation_show(&action, 1, "test", "", ""));
    assert(!s_overlay && !s_action_count);
    button_confirmation_cancel();

    char output[128] = {};
    mqtt_binding_init();
    assert(mqtt_binding_resolve("topic", output, sizeof(output)) == BINDING_RESOLVER_UNAVAILABLE);
    list_provider_pads_init();
    ListItem items[LIST_MAX_ITEMS] = {};
    assert(registered_provider && registered_provider->provide(items, LIST_MAX_ITEMS) == 0);

    button_defaults_init();
    const ButtonDefaults* fallback = button_defaults_get();
    assert(fallback && fallback->bg_color[0] == '\0');
    assert(fallback->layout.spacing_px == 6);
    assert(button_defaults_get_pixel_shift_distance() == 4);
    assert(!strcmp(fallback->shadow.color, "#101010"));
    const char* json = "{\"bg_color\":\"#123456\"}";
    assert(!button_defaults_save_raw(reinterpret_cast<const uint8_t*>(json), strlen(json)));
    assert(storage_test_files.empty());

    allocation_fail = false;
    button_defaults_init();
    const ButtonDefaults* defaults = button_defaults_get();
    assert(defaults && defaults != fallback && defaults->layout.spacing_px == 6);
    assert(button_defaults_save_raw(reinterpret_cast<const uint8_t*>(json), strlen(json)));
    assert(!strcmp(defaults->bg_color, "#123456") && rebuilds == 1);
    button_defaults_init();
    assert(button_defaults_get() == defaults && rebuilds == 2);

    mqtt_binding_init();
    assert(mqtt_binding_resolve("topic;value", output, sizeof(output)) == BINDING_RESOLVER_RESOLVED);
    assert(!strcmp(output, "42"));
    list_provider_pads_invalidate();
    assert(registered_provider->provide(items, LIST_MAX_ITEMS) == 2);
    assert(!strcmp(items[0].id, "pad_0") && !strcmp(items[0].label, "Pad 0: Home"));
    assert(!strcmp(items[1].label, "Pad 2"));
    assert(registered_provider->provide(nullptr, LIST_MAX_ITEMS) == 0);
    assert(registered_provider->provide(items, 0) == 0);
    assert(registered_provider->provide(items, 1) == 1);

    assert(button_confirmation_show(&action, 1, "test", "", ""));
    memset(&action, 0, sizeof(action));
    on_confirm(nullptr);
    assert(dispatched == 1 && !s_overlay && !s_action_count);
    strlcpy(action.type, "screen", sizeof(action.type));
    const unsigned allocated = allocations;
    allocation_fail = true;
    assert(button_confirmation_show(&action, 1, "test", "", ""));
    button_confirmation_cancel();
    assert(!s_overlay && !s_action_count && dispatched == 1);
    mqtt_binding_init();
    list_provider_pads_invalidate();
    button_defaults_init();
    assert(allocations == allocated);
    assert(button_defaults_get() == defaults);
}
'''

ALLOCATOR_PRELUDE = r'''
namespace allocator_probe {
static bool psram_present = true;
static bool external_failure = false;
static bool internal_failure = false;
static unsigned external_calls = 0;
static unsigned internal_calls = 0;
bool psramFound() { return psram_present; }
void* heap_caps_calloc(size_t count, size_t size, uint32_t caps) {
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    ++external_calls;
    return external_failure ? nullptr : ::calloc(count, size);
}
void* calloc(size_t count, size_t size) {
    ++internal_calls;
    return internal_failure ? nullptr : ::calloc(count, size);
}
'''

ALLOCATOR_CHECKS = r'''
void run_checks() {
    char* memory = static_cast<char*>(config_psram_alloc(64, "probe"));
    assert(memory && memory[0] == 0 && memory[63] == 0);
    assert(external_calls == 1 && internal_calls == 0);
    free(memory);
    external_failure = true;
    memory = static_cast<char*>(config_psram_alloc(64, "probe"));
    assert(memory && memory[0] == 0 && memory[63] == 0);
    assert(external_calls == 2 && internal_calls == 1);
    free(memory);
    psram_present = false;
    memory = static_cast<char*>(config_psram_alloc(64, "probe"));
    assert(memory && external_calls == 2 && internal_calls == 2);
    free(memory);
    internal_failure = true;
    assert(!config_psram_alloc(64, "probe"));
    psram_present = true;
    assert(!config_psram_alloc(64, "probe"));
    assert(external_calls == 3 && internal_calls == 4);
}
}
'''


def main():
    compiler = shutil.which("g++")
    if not compiler:
        raise SystemExit("g++ is required")
    json_headers = pathlib.Path.home() / "Arduino/libraries/ArduinoJson/src"
    sources = ["button_defaults.cpp", "list_provider_pads.cpp", "mqtt_binding.cpp", "button_confirmation.cpp"]
    with tempfile.TemporaryDirectory(prefix="static-state-") as directory:
        source = pathlib.Path(directory) / "test.cpp"
        executable = pathlib.Path(directory) / "test"
        allocator = ALLOCATOR_PRELUDE + production("config_psram.cpp") + ALLOCATOR_CHECKS
        source.write_text(PRELUDE + allocator + "\n#undef TAG\n"
                  + "\n#undef TAG\n".join(production(name) for name in sources) + CHECKS)
        subprocess.run([
            compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror", "-Wno-deprecated-declarations",
            "-iquote", str(ROOT / "tests"),
            "-I", str(ROOT / "tests/timer_test_overrides"),
            "-I", str(ROOT / "tests"), "-I", str(ROOT / "src/app"),
            "-I", str(json_headers), str(source), str(ROOT / "tests/stubs.cpp"),
            "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)
    print("Static state allocation, failure, retry, and reuse checks passed")


if __name__ == "__main__":
    main()