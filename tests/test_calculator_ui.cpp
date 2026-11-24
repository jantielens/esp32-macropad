#include <gtest/gtest.h>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "native_extension_ui.h"

extern "C" bool native_extension_create_instance(const NativeExtensionHostApi*, void*, uint32_t,
                                                   void*, const char*);
extern "C" void native_extension_destroy_instance(const NativeExtensionHostApi*, void*, uint32_t);
extern "C" void native_extension_shutdown(const NativeExtensionHostApi*, void*);

namespace {

struct Context { void* data; };
int allocations = 0;
int registrations = 0;
int fail_registration = -1;
int interactions = 0;
int tap_sounds = 0;
bool disable_animations = false;
bool feedback_enabled = false;
char selected_font[32];
char measured_font[32];

void* allocate(size_t size) { ++allocations; return std::malloc(size); }
void release(void* ptr) { if (ptr) --allocations; std::free(ptr); }
void* get_data(void* context) { return static_cast<Context*>(context)->data; }
void set_data(void* context, void* data) { static_cast<Context*>(context)->data = data; }
void log_message(NativeExtensionLogLevel, const char*) {}
void* create_button(void* parent) {
    return native_extension_ui_button_create(parent);
}
void* create_label(void* parent) { return lv_label_create(static_cast<lv_obj_t*>(parent)); }
void set_text(void* obj, const char* text) { lv_label_set_text(static_cast<lv_obj_t*>(obj), text); }
void center(void* obj) { lv_obj_center(static_cast<lv_obj_t*>(obj)); }
void delete_obj(void* obj) { lv_obj_delete(static_cast<lv_obj_t*>(obj)); }
void set_size(void* obj, int32_t w, int32_t h) { lv_obj_set_size(static_cast<lv_obj_t*>(obj), w, h); }
void set_pos(void* obj, int32_t x, int32_t y) { lv_obj_set_pos(static_cast<lv_obj_t*>(obj), x, y); }
void align(void* obj, NativeExtensionAlign alignment, int32_t x, int32_t y) {
    EXPECT_TRUE(native_extension_ui_align(obj, alignment, x, y));
}
int32_t width(void* obj) { return lv_obj_get_width(static_cast<lv_obj_t*>(obj)); }
int32_t height(void* obj) { return lv_obj_get_height(static_cast<lv_obj_t*>(obj)); }
void clickable(void* obj, bool enabled) {
    lv_obj_set_clickable(static_cast<lv_obj_t*>(obj), enabled);
}
void background(void* obj, uint32_t color, uint8_t opacity) {
    lv_obj_set_style_bg_color(static_cast<lv_obj_t*>(obj), lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(static_cast<lv_obj_t*>(obj), opacity, 0);
}
void border(void* obj, uint32_t color, int32_t w, int32_t radius) {
    lv_obj_set_style_border_color(static_cast<lv_obj_t*>(obj), lv_color_hex(color), 0);
    lv_obj_set_style_border_width(static_cast<lv_obj_t*>(obj), w, 0);
    lv_obj_set_style_radius(static_cast<lv_obj_t*>(obj), radius, 0);
}
void text_color(void* obj, uint32_t color) {
    lv_obj_set_style_text_color(static_cast<lv_obj_t*>(obj), lv_color_hex(color), 0);
}
void padding(void* obj, int32_t pad) { lv_obj_set_style_pad_all(static_cast<lv_obj_t*>(obj), pad, 0); }
void font(void*, uint8_t) {}
void named_font(void*, const char* name, uint8_t) {
    std::strncpy(selected_font, name, sizeof(selected_font) - 1);
    selected_font[sizeof(selected_font) - 1] = '\0';
}
void wrap(void* label, int32_t width) {
    lv_obj_set_width(static_cast<lv_obj_t*>(label), width);
    lv_label_set_long_mode(static_cast<lv_obj_t*>(label), LV_LABEL_LONG_WRAP);
}
void feedback(void* button, bool enabled) {
    feedback_enabled = enabled;
    native_extension_ui_button_set_press_feedback(button, enabled);
}
void* add_event(void* obj, NativeExtensionLvglEventCode code, NativeExtensionUiEventBinding* binding) {
    if (++registrations == fail_registration) return nullptr;
    return native_extension_ui_event_add(obj, code, binding);
}
void interaction() { ++interactions; }
void tap_feedback() { ++tap_sounds; }
bool disabled() { return disable_animations; }
int32_t measure(const char* text, const char* name, uint8_t size) {
    std::strncpy(measured_font, name, sizeof(measured_font) - 1);
    measured_font[sizeof(measured_font) - 1] = '\0';
    return static_cast<int32_t>(std::strlen(text) * size / 2);
}
bool get_button(void*, uint32_t, NativeExtensionButtonSnapshot* out) {
    *out = {};
    out->background_rgb = 0x000000;
    out->foreground_rgb = 0xffffff;
    return true;
}

class CalculatorUi : public testing::Test {
protected:
    NativeExtensionCoreApi core{};
    NativeExtensionUiApi ui{};
    NativeExtensionCanvasApi canvas{};
    NativeExtensionButtonApi button{};
    NativeExtensionHostApi host{};
    Context context{};
    lv_display_t* display = nullptr;
    lv_obj_t* root = nullptr;

    void SetUp() override {
        lv_init();
        display = lv_display_create(480, 480);
        root = lv_obj_create(lv_screen_active());
        lv_obj_remove_style_all(root);
        lv_obj_set_size(root, 320, 320);
        lv_obj_update_layout(root);
        allocations = registrations = interactions = tap_sounds = 0;
        fail_registration = -1;
        disable_animations = feedback_enabled = false;
        selected_font[0] = measured_font[0] = '\0';
        core.size = sizeof(core);
        core.alloc = allocate;
        core.free = release;
        core.context_get_data = get_data;
        core.context_set_data = set_data;
        core.math_sqrt = std::sqrt;
        core.log = log_message;
        ui.size = sizeof(ui);
        ui.label_create = create_label;
        ui.label_set_text = set_text;
        ui.obj_center = center;
        ui.obj_delete = delete_obj;
        ui.obj_set_size = set_size;
        ui.obj_set_pos = set_pos;
        ui.obj_align = align;
        ui.obj_get_width = width;
        ui.obj_get_height = height;
        ui.obj_set_clickable = clickable;
        ui.obj_set_bg_color = background;
        ui.obj_set_border = border;
        ui.obj_set_text_color = text_color;
        ui.obj_set_padding = padding;
        ui.button_create = create_button;
        ui.label_set_font_size = font;
        ui.button_set_press_feedback = feedback;
        ui.event_add = add_event;
        ui.event_remove = native_extension_ui_event_remove;
        ui.interaction_changed = interaction;
        ui.display_disables_animations = disabled;
        ui.label_set_wrap = wrap;
        ui.label_set_font = named_font;
        ui.tap_feedback = tap_feedback;
        canvas.size = sizeof(canvas);
        canvas.canvas_measure_text = measure;
        button.size = sizeof(button);
        button.get = get_button;
        host.abi_version = NATIVE_EXTENSION_ABI_VERSION;
        host.size = sizeof(host);
        host.core = &core;
        host.ui = &ui;
        host.canvas = &canvas;
        host.button = &button;
    }

    void TearDown() override {
        native_extension_shutdown(&host, &context);
        lv_display_delete(display);
        lv_deinit();
        EXPECT_EQ(allocations, 0);
    }
    lv_obj_t* panel() { return lv_obj_get_child(root, 0); }
    void press(int index) { lv_obj_send_event(lv_obj_get_child(panel(), 1 + index), LV_EVENT_CLICKED, nullptr); }
    const char* result() {
        return lv_label_get_text(lv_obj_get_child(lv_obj_get_child(panel(), 0), 0));
    }
};

TEST_F(CalculatorUi, RoutesKeysAndReleasesInstance) {
    ASSERT_TRUE(native_extension_create_instance(&host, &context, 1, root, ""));
    EXPECT_TRUE(feedback_enabled);
    EXPECT_STREQ(result(), "0");
    press(16); press(23); press(17); press(13); press(10); press(22);
    EXPECT_STREQ(result(), "20");
    EXPECT_GT(interactions, 0);
    native_extension_destroy_instance(&host, &context, 1);
    EXPECT_EQ(lv_obj_get_child_count(root), 0u);
    EXPECT_EQ(context.data, nullptr);
}

TEST_F(CalculatorUi, RequestsTapFeedbackForEachKeyEvenWithoutDisplayChange) {
    ASSERT_TRUE(native_extension_create_instance(&host, &context, 1, root, ""));
    press(20);
    press(20);
    press(0);
    EXPECT_STREQ(result(), "0");
    EXPECT_EQ(tap_sounds, 3);
    native_extension_destroy_instance(&host, &context, 1);
}

TEST_F(CalculatorUi, KeepsDisplayRightAlignedWhenValueChanges) {
    ASSERT_TRUE(native_extension_create_instance(&host, &context, 1, root, ""));
    auto* box = lv_obj_get_child(panel(), 0);
    auto* label = lv_obj_get_child(box, 0);
    const auto check_alignment = [&]() {
        lv_obj_update_layout(root);
        lv_area_t box_area, label_area;
        lv_obj_get_coords(box, &box_area);
        lv_obj_get_coords(label, &label_area);
        EXPECT_EQ(box_area.x2 - label_area.x2,
                  4 + lv_obj_get_style_border_width(box, LV_PART_MAIN));
        EXPECT_LE(std::abs((label_area.y1 + label_area.y2) - (box_area.y1 + box_area.y2)), 1);
    };
    check_alignment();
    press(15);
    press(16);
    EXPECT_STREQ(result(), "12");
    check_alignment();
    native_extension_destroy_instance(&host, &context, 1);
}

TEST_F(CalculatorUi, InstancesAreIndependent) {
    auto* second_root = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(second_root);
    lv_obj_set_size(second_root, 320, 320);
    lv_obj_update_layout(second_root);
    ASSERT_TRUE(native_extension_create_instance(&host, &context, 1, root, ""));
    ASSERT_TRUE(native_extension_create_instance(&host, &context, 2, second_root, ""));
    press(16);
    auto* second_panel = lv_obj_get_child(second_root, 0);
    lv_obj_send_event(lv_obj_get_child(second_panel, 16), LV_EVENT_CLICKED, nullptr);
    EXPECT_STREQ(result(), "2");
    EXPECT_STREQ(lv_label_get_text(lv_obj_get_child(lv_obj_get_child(second_panel, 0), 0)), "1");
    native_extension_destroy_instance(&host, &context, 1);
    lv_obj_send_event(lv_obj_get_child(second_panel, 17), LV_EVENT_CLICKED, nullptr);
    EXPECT_STREQ(lv_label_get_text(lv_obj_get_child(lv_obj_get_child(second_panel, 0), 0)), "12");
    native_extension_destroy_instance(&host, &context, 2);
}

TEST_F(CalculatorUi, EpaperAndTooSmall) {
    disable_animations = true;
    ASSERT_TRUE(native_extension_create_instance(&host, &context, 1, root, ""));
    EXPECT_FALSE(feedback_enabled);
    lv_obj_update_layout(root);
    unsigned invalidations = 0;
    const uint32_t event_index = lv_display_get_event_count(display);
    lv_display_add_event_cb(display, [](lv_event_t* event) {
        ++*static_cast<unsigned*>(lv_event_get_user_data(event));
    }, LV_EVENT_INVALIDATE_AREA, &invalidations);
    auto* key = lv_obj_get_child(panel(), 1);
    lv_obj_add_state(key, LV_STATE_PRESSED);
    lv_obj_remove_state(key, LV_STATE_PRESSED);
    EXPECT_EQ(invalidations, 0u);
    press(20);
    EXPECT_EQ(tap_sounds, 1);
    EXPECT_TRUE(lv_display_remove_event(display, event_index));
    native_extension_destroy_instance(&host, &context, 1);
    lv_obj_set_size(root, 120, 120);
    lv_obj_update_layout(root);
    ASSERT_TRUE(native_extension_create_instance(&host, &context, 2, root, "{}"));
    EXPECT_STREQ(lv_label_get_text(lv_obj_get_child(panel(), 0)), "Make widget bigger");
    lv_obj_update_layout(panel());
    EXPECT_EQ(lv_obj_get_width(lv_obj_get_child(panel(), 0)), 120);
    native_extension_destroy_instance(&host, &context, 2);
}

TEST_F(CalculatorUi, DisplayPolicyDisablesFeedbackInNormalMode) {
    disable_animations = true;
    ASSERT_TRUE(native_extension_create_instance(&host, &context, 1, root, ""));
    EXPECT_FALSE(feedback_enabled);
    native_extension_destroy_instance(&host, &context, 1);
}

TEST_F(CalculatorUi, StylesCategoriesAndPreservesColorFallbacks) {
    const char* config =
        "{\"font\":\"@builtin\",\"corner_radius\":8,"
        "\"keys_background\":\"#123456\",\"keys_foreground\":\"#ABCDEF\","
        "\"keys_overrides\":{\"numbers\":{\"background\":\"#010203\"},"
        "\"operators\":{\"foreground\":\"#112233\"},"
        "\"functions\":{\"background\":\"#445566\",\"foreground\":\"#778899\"},"
        "\"memory\":{\"background\":\"#FF0000\"}}}";
    ASSERT_TRUE(native_extension_create_instance(&host, &context, 1, root, config));
    const auto check = [&](int index, uint32_t bg, uint32_t fg) {
        lv_obj_t* object = lv_obj_get_child(panel(), 1 + index);
        EXPECT_EQ(lv_color_to_u32(lv_obj_get_style_bg_color(object, LV_PART_MAIN)) & 0xffffffu, bg);
        EXPECT_EQ(lv_color_to_u32(lv_obj_get_style_text_color(object, LV_PART_MAIN)) & 0xffffffu, fg);
        EXPECT_EQ(lv_color_to_u32(lv_obj_get_style_border_color(object, LV_PART_MAIN)) & 0xffffffu, fg);
        EXPECT_EQ(lv_obj_get_style_radius(object, LV_PART_MAIN), 8);
    };
    check(5, 0x010203, 0xabcdef);
    check(8, 0x123456, 0x112233);
    check(4, 0x445566, 0x778899);
    check(0, 0xff0000, 0xabcdef);
    EXPECT_STREQ(selected_font, "@builtin");
    EXPECT_STREQ(measured_font, "@builtin");
    EXPECT_TRUE(feedback_enabled);
    press(5);
    EXPECT_STREQ(selected_font, "@builtin");
    EXPECT_STREQ(measured_font, "@builtin");
    native_extension_destroy_instance(&host, &context, 1);
}

TEST_F(CalculatorUi, DefaultAndIndividualOverridesFallBackIndependently) {
    ASSERT_TRUE(native_extension_create_instance(&host, &context, 1, root,
        "{\"corner_radius\":0,\"keys_overrides\":{\"operators\":{\"background\":\"#223344\"}}}"));
    lv_obj_t* number = lv_obj_get_child(panel(), 6);
    lv_obj_t* op = lv_obj_get_child(panel(), 9);
    EXPECT_EQ(lv_color_to_u32(lv_obj_get_style_bg_color(number, LV_PART_MAIN)) & 0xffffffu, 0u);
    EXPECT_EQ(lv_color_to_u32(lv_obj_get_style_text_color(op, LV_PART_MAIN)) & 0xffffffu, 0xffffffu);
    EXPECT_EQ(lv_color_to_u32(lv_obj_get_style_bg_color(op, LV_PART_MAIN)) & 0xffffffu, 0x223344u);
    EXPECT_EQ(lv_obj_get_style_radius(op, LV_PART_MAIN), 0);
    native_extension_destroy_instance(&host, &context, 1);
}

TEST_F(CalculatorUi, StylesDisplaySeparatelyFromKeys) {
    ASSERT_TRUE(native_extension_create_instance(&host, &context, 1, root,
        "{\"font\":\"@builtin\",\"display_background\":\"#DDEECC\","
        "\"display_foreground\":\"#112233\",\"display_border_color\":\"#445566\","
        "\"display_corner_radius\":4,\"display_font\":\"custom\","
        "\"display_alignment\":\"left\"}"));
    auto* box = lv_obj_get_child(panel(), 0);
    auto* label = lv_obj_get_child(box, 0);
    EXPECT_EQ(lv_color_to_u32(lv_obj_get_style_bg_color(box, LV_PART_MAIN)) & 0xffffffu, 0xddeeccu);
    EXPECT_EQ(lv_color_to_u32(lv_obj_get_style_text_color(box, LV_PART_MAIN)) & 0xffffffu, 0x112233u);
    EXPECT_EQ(lv_color_to_u32(lv_obj_get_style_border_color(box, LV_PART_MAIN)) & 0xffffffu, 0x445566u);
    EXPECT_EQ(lv_obj_get_style_radius(box, LV_PART_MAIN), 4);
    EXPECT_EQ(lv_obj_get_style_align(label, LV_PART_MAIN), LV_ALIGN_LEFT_MID);
    EXPECT_STREQ(selected_font, "@builtin");
    press(16);
    EXPECT_STREQ(selected_font, "custom");
    EXPECT_STREQ(measured_font, "custom");
    native_extension_destroy_instance(&host, &context, 1);
}

TEST_F(CalculatorUi, DisplayOverridesFallBackIndependently) {
    ASSERT_TRUE(native_extension_create_instance(&host, &context, 1, root,
        "{\"font\":\"@builtin\",\"display_foreground\":\"#123456\","
        "\"display_corner_radius\":\"auto\",\"display_alignment\":\"center\"}"));
    auto* box = lv_obj_get_child(panel(), 0);
    auto* label = lv_obj_get_child(box, 0);
    EXPECT_EQ(lv_color_to_u32(lv_obj_get_style_bg_color(box, LV_PART_MAIN)) & 0xffffffu, 0u);
    EXPECT_EQ(lv_color_to_u32(lv_obj_get_style_text_color(box, LV_PART_MAIN)) & 0xffffffu, 0x123456u);
    EXPECT_EQ(lv_color_to_u32(lv_obj_get_style_border_color(box, LV_PART_MAIN)) & 0xffffffu, 0x123456u);
    EXPECT_EQ(lv_obj_get_style_radius(box, LV_PART_MAIN), 2);
    EXPECT_EQ(lv_obj_get_style_align(label, LV_PART_MAIN), LV_ALIGN_CENTER);
    press(16);
    EXPECT_STREQ(selected_font, "@builtin");
    EXPECT_STREQ(measured_font, "@builtin");
    native_extension_destroy_instance(&host, &context, 1);
}

TEST_F(CalculatorUi, DecodesEscapedFontName) {
    ASSERT_TRUE(native_extension_create_instance(&host, &context, 1, root,
                                                  "{\"font\":\"caf\\u00e9\"}"));
    EXPECT_STREQ(selected_font, "caf\xc3\xa9");
    EXPECT_STREQ(measured_font, "caf\xc3\xa9");
    native_extension_destroy_instance(&host, &context, 1);
}

TEST_F(CalculatorUi, AcceptsCompleteConfigurationAndAutomaticRadius) {
    const char* config =
        "{\"font\":\"default\",\"corner_radius\":\"auto\","
        "\"keys_background\":\"#FFFFFF\",\"keys_foreground\":\"#111111\","
        "\"keys_overrides\":{\"numbers\":{\"background\":\"#FFFFFF\",\"foreground\":\"#111111\"},"
        "\"operators\":{\"background\":\"#333333\",\"foreground\":\"#FFFFFF\"},"
        "\"functions\":{\"background\":\"#DDDDDD\",\"foreground\":\"#111111\"},"
        "\"memory\":{\"background\":\"#EEEEEE\",\"foreground\":\"#111111\"}}}";
    ASSERT_TRUE(native_extension_create_instance(&host, &context, 1, root, config));
    EXPECT_EQ(lv_obj_get_style_radius(lv_obj_get_child(panel(), 1), LV_PART_MAIN), 3);
    native_extension_destroy_instance(&host, &context, 1);
}

TEST_F(CalculatorUi, RejectsInvalidStyles) {
    const char* invalid[] = {
        "{\"keys_background\":\"#123\"}", "{\"corner_radius\":-1}", "{\"corner_radius\":32768}",
        "{\"corner_radius\":01}", "{\"keys_overrides\":{\"other\":{}}}",
        "{\"keys_overrides\":{\"numbers\":{\"background\":\"#000000\",}}}",
        "{\"font\":\"\"}", "{\"keys_foreground\":\"#0000000\"}",
        "{\"font\":\"default\",\"font\":\"@builtin\"}",
        "{\"display_background\":\"#123\"}", "{\"display_foreground\":\"red\"}",
        "{\"display_border_color\":\"#FFFFFFF\"}", "{\"display_corner_radius\":-1}",
        "{\"display_corner_radius\":32768}", "{\"display_corner_radius\":01}",
        "{\"display_font\":\"\"}", "{\"display_alignment\":\"justify\"}",
        "{\"display_alignment\":\"left\",\"display_alignment\":\"right\"}",
        "{\"display_style\":\"normal\"}", "{\"display_style\":\"epaper\"}"
    };
    for (const char* config : invalid) {
        EXPECT_FALSE(native_extension_create_instance(&host, &context, 1, root, config)) << config;
        EXPECT_EQ(context.data, nullptr);
    }
}

TEST_F(CalculatorUi, RejectsHostWithoutNamedFontService) {
    ui.size = offsetof(NativeExtensionUiApi, label_set_font);
    EXPECT_FALSE(native_extension_create_instance(&host, &context, 1, root, ""));
    EXPECT_EQ(allocations, 0);
    EXPECT_EQ(lv_obj_get_child_count(root), 0u);
}

TEST_F(CalculatorUi, RejectsHostWithoutTapFeedbackService) {
    ui.size = offsetof(NativeExtensionUiApi, tap_feedback);
    EXPECT_FALSE(native_extension_create_instance(&host, &context, 1, root, ""));
    EXPECT_EQ(allocations, 0);
    EXPECT_EQ(lv_obj_get_child_count(root), 0u);
}

TEST_F(CalculatorUi, FailedRegistrationCleansUpAndInvalidConfigFails) {
    fail_registration = 4;
    EXPECT_FALSE(native_extension_create_instance(&host, &context, 1, root, ""));
    EXPECT_EQ(lv_obj_get_child_count(root), 0u);
    EXPECT_EQ(context.data, nullptr);
    EXPECT_FALSE(native_extension_create_instance(&host, &context, 1, root,
                                                   "{\"display_style\":\"unknown\"}"));
    EXPECT_EQ(allocations, 0);
}

} // namespace
