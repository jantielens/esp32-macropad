#!/usr/bin/env python3
import pathlib
import subprocess
import tempfile


root = pathlib.Path(__file__).resolve().parents[1]
source = (root / "src/app/widgets/gauge_widget.cpp").read_text()


def function(name):
    start = source.index("static ", source.rfind("\n", 0, source.index(name + "(")))
    body = source.index("{", start)
    depth = 1
    end = body + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


harness = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#define HAS_MQTT 1
#define CONFIG_BINDABLE_SHORT_LEN 64
#define LV_OBJ_FLAG_HIDDEN 1
#define strlcpy test_strlcpy
enum lv_part_t { LV_PART_MAIN = 0, LV_PART_INDICATOR = 1 };
using lv_color_t = uint32_t;
struct lv_obj_t { std::string text; bool hidden = false; lv_color_t color = 0; };
struct GaugeConfig {};
struct GaugeState {};
static std::string binding;
static int placements = 0;
static int text_sets = 0;
static int color_sets = 0;
static bool binding_template_has_bindings(const char* text) { return text[0] == '['; }
static void strlcpy(char* out, const char* text, size_t size) {
    std::strncpy(out, text, size - 1); out[size - 1] = '\0';
}
static void binding_template_resolve(const char*, char* out, size_t size) {
    strlcpy(out, binding.c_str(), size);
}
static const char* lv_label_get_text(lv_obj_t* label) { return label->text.c_str(); }
static void lv_label_set_text(lv_obj_t* label, const char* text) { label->text = text; ++text_sets; }
static void lv_obj_clear_flag(lv_obj_t* label, int) { label->hidden = false; }
static void lv_obj_add_flag(lv_obj_t* label, int) { label->hidden = true; }
static void gauge_place_start_label(lv_obj_t* label, const GaugeConfig*, const GaugeState*, uint8_t) {
    if (!label->hidden) ++placements;
}
static lv_color_t lv_obj_get_style_arc_color(lv_obj_t* arc, lv_part_t) { return arc->color; }
static lv_color_t lv_obj_get_style_text_color(lv_obj_t* label, lv_part_t) { return label->color; }
static bool lv_color_eq(lv_color_t first, lv_color_t second) { return first == second; }
static void lv_obj_set_style_text_color(lv_obj_t* label, lv_color_t color, int) {
    label->color = color; ++color_sets;
}
'''
harness += "\n".join(function(name) for name in (
    "gauge_set_start_label_text", "gauge_update_start_label", "gauge_sync_start_label_color"))
harness += r'''
int main() {
    lv_obj_t label;
    GaugeConfig config;
    GaugeState state;
    binding = "Power";
    gauge_update_start_label(&label, "[mqtt:value]", &config, &state, 0);
    assert(placements == 1 && text_sets == 1 && !label.hidden);
    for (int tick = 0; tick < 100; ++tick)
        gauge_update_start_label(&label, "[mqtt:value]", &config, &state, 0);
    assert(placements == 1 && text_sets == 1);
    binding = "Longer caption";
    gauge_update_start_label(&label, "[mqtt:value]", &config, &state, 0);
    assert(placements == 2 && text_sets == 2);
    binding = "";
    gauge_update_start_label(&label, "[mqtt:value]", &config, &state, 0);
    assert(label.hidden && placements == 2);
    binding = "Power";
    gauge_update_start_label(&label, "[mqtt:value]", &config, &state, 0);
    assert(!label.hidden && placements == 3);
    lv_obj_t rebuilt;
    gauge_update_start_label(&rebuilt, "Power", &config, &state, 3);
    assert(placements == 4);
    lv_obj_t arc;
    gauge_sync_start_label_color(&label, &arc);
    assert(color_sets == 0);
    arc.color = 0x123456;
    gauge_sync_start_label_color(&label, &arc);
    gauge_sync_start_label_color(&label, &arc);
    assert(color_sets == 1 && label.color == arc.color);
}
'''

with tempfile.TemporaryDirectory() as directory:
    test_source = pathlib.Path(directory) / "caption.cpp"
    executable = pathlib.Path(directory) / "caption"
    test_source.write_text(harness)
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    str(test_source), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print("PASS: gauge caption caching")