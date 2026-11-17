#include "welcome_pad.h"
#include "pad_config.h"
#include "pad_layout.h"
#include "storage.h"

#include <ArduinoJson.h>
#include <cassert>
#include <cstdio>
#include <string>

FakeFontStorage LittleFS;
FakeFontStorage SD_MMC;
static unsigned mounts, saves, marker_writes;
static bool save_ok = true, marker_ok = true;
static std::string saved_json;

extern const lv_font_t lv_font_montserrat_12 = {};
extern const lv_font_t lv_font_montserrat_14 = {};
extern const lv_font_t lv_font_montserrat_18 = {};
extern const lv_font_t lv_font_montserrat_24 = {};
extern const lv_font_t lv_font_montserrat_32 = {};
extern const lv_font_t lv_font_montserrat_36 = {};

#if USE_SD_STORAGE && HAS_AUTO_SHUTDOWN
static bool mutation_allowed = true;
static unsigned writers = 0;
bool ota_activity_try_begin_config_write() {
    if (!mutation_allowed) return false;
    ++writers;
    return true;
}
void ota_activity_finish_config_write() {
    assert(writers > 0);
    --writers;
}
#endif

bool storage_mount() { ++mounts; return Storage.mounted; }
void pad_config_path(uint8_t index, char* out, size_t size) {
    snprintf(out, size, "/config/pad_%u.json", index);
}
bool storage_write_atomic(const char* path, const uint8_t* bytes, size_t length) {
    if (strcmp(path, "/config/pad_0.json") == 0) {
        ++saves;
        if (!save_ok) return false;
        saved_json.assign(reinterpret_cast<const char*>(bytes), length);
    } else {
        ++marker_writes;
        if (!marker_ok) return false;
    }
#if USE_SD_STORAGE && HAS_AUTO_SHUTDOWN
    assert(writers > 0);
#endif
    Storage.files[path] = {bytes, bytes + length};
    return true;
}

static void reset() {
    Storage = {};
    mounts = saves = marker_writes = 0;
    save_ok = marker_ok = true;
    saved_json.clear();
}

static void check_round_layout(JsonObjectConst document, uint16_t diameter) {
    const JsonArrayConst buttons = document["buttons"];
    uint8_t columns[3], rows[3], column_spans[3], row_spans[3];
    for (unsigned index = 0; index < buttons.size(); ++index) {
        columns[index] = buttons[index]["col"];
        rows[index] = buttons[index]["row"];
        column_spans[index] = buttons[index]["col_span"];
        row_spans[index] = buttons[index]["row_span"] | uint8_t(1);
    }
    PadRect rects[3];
    pad_compute_grid(document["cols"], document["rows"], diameter, diameter,
                     columns, rows, column_spans, row_spans, 3, rects);
    const int radius = diameter / 2;
    for (const PadRect& rect : rects) {
        assert(rect.x >= 0 && rect.y >= 0 && rect.w > 0 && rect.h > 0);
        assert(rect.x + rect.w <= diameter && rect.y + rect.h <= diameter);
        assert(abs(2 * rect.x + rect.w - diameter) <= 6);
        const int center_y = rect.y + rect.h / 2;
        const int half_width = rect.w / 2;
        assert(half_width * half_width + (center_y - radius) * (center_y - radius) < radius * radius);
    }
    assert(rects[0].y + rects[0].h < rects[1].y);
    assert(rects[1].y + rects[1].h < rects[2].y);
    assert(rects[1].w > rects[0].w && rects[1].w > rects[2].w);
    assert(abs(rects[0].y + rects[2].y + rects[2].h - diameter) <= 6);
}

int main(int argc, char**) {
    reset();
    assert(welcome_pad_provision(true));
    assert(mounts == 0 && saves == 0 && marker_writes == 0);
    assert(Storage.exists_calls.empty() && Storage.open_calls.empty());

    reset();
    assert(welcome_pad_provision(false));
    assert(saves == 1 && marker_writes == 1);
    JsonDocument document;
    assert(!deserializeJson(document, saved_json));
    const unsigned cols = document["cols"], rows = document["rows"];
    assert(cols > 0 && cols <= MAX_GRID_COLS && rows > 0 && rows <= MAX_GRID_ROWS);
    assert(document["bg_color"].isNull());
    JsonArrayConst buttons = document["buttons"];
    assert(buttons.size() == 3);
    for (unsigned index = 0; index < buttons.size(); ++index) {
        const JsonObjectConst button = buttons[index];
        const unsigned col = button["col"], row = button["row"];
        assert(col + (button["col_span"] | 1U) <= cols);
        assert(row + (button["row_span"] | 1U) <= rows);
        assert(button["actions"].isNull() && button["lp_actions"].isNull());
        assert(button["bg_color"].isNull() && button["fg_color"].isNull());
        for (const char* label : {"label_top", "label_center", "label_bottom"})
            assert(strlen(button[label] | "") < CONFIG_LABEL_MAX_LEN);
    }
    assert(document["buttons"][0]["label_center"] == "Welcome!");
    check_round_layout(document.as<JsonObjectConst>(), 240);
    check_round_layout(document.as<JsonObjectConst>(), 466);
    if (argc > 1) puts(saved_json.c_str());

    Storage.remove("/config/pad_0.json");
    assert(welcome_pad_provision(false));
    assert(saves == 1 && !Storage.exists("/config/pad_0.json"));

    for (const char* path : {"/config/pad_5.json", "/config/pad_5.json.bak", "/config/pad_5.json.tmp"}) {
        reset();
        Storage.files[path] = {'x'};
        assert(welcome_pad_provision(false));
        assert(saves == 0 && marker_writes == 1);
        assert(Storage.files[path] == std::vector<uint8_t>{'x'});
        Storage.files.erase(path);
        assert(welcome_pad_provision(false));
        assert(saves == 0);
    }

    reset();
    Storage.mounted = false;
    assert(!welcome_pad_provision(false));
    assert(saves == 0 && marker_writes == 0);

    reset();
    save_ok = false;
    assert(!welcome_pad_provision(false));
    assert(marker_writes == 0);
    save_ok = true;
    assert(welcome_pad_provision(false));
    assert(saves == 2 && marker_writes == 1);

    reset();
    marker_ok = false;
    assert(!welcome_pad_provision(false));
    marker_ok = true;
    assert(welcome_pad_provision(false));
    assert(saves == 1 && marker_writes == 2);
#if USE_SD_STORAGE && HAS_AUTO_SHUTDOWN
    reset();
    mutation_allowed = false;
    assert(!welcome_pad_provision(false));
    assert(saves == 0 && marker_writes == 0 && writers == 0);
    mutation_allowed = true;
    assert(welcome_pad_provision(false));
    assert(writers == 0);
#endif
    return 0;
}
