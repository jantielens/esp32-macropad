#include <gtest/gtest.h>
#include <vector>

#include "widgets/slider_numeric.h"
#include "widgets/slider_widget.cpp"
#include "widgets/option_selector_widget.cpp"
#include "font_store.h"

static const uint8_t slider_font_fixture[] = {
#include "fixtures/BebasNeue.inc"
};
static unsigned g_font_loads;
static unsigned g_font_buffers;

extern "C" unsigned long millis() { return 0; }
size_t heap_caps_get_free_size(uint32_t) { return 8192; }
size_t heap_caps_get_largest_free_block(uint32_t) { return 4096; }
bool ota_activity_is_active() { return false; }
bool font_store_validate_name(const char* name) { return name && *name && strlen(name) < FONT_NAME_MAX_LEN; }
uint8_t* font_store_load(const char* name, size_t& length) {
    ++g_font_loads;
    if (strcmp(name, "missing") == 0) return nullptr;
    length = sizeof(slider_font_fixture);
    auto* data = static_cast<uint8_t*>(malloc(length));
    if (data) {
        memcpy(data, slider_font_fixture, length);
        ++g_font_buffers;
    }
    return data;
}
void heap_caps_free(void* data) {
    if (data) --g_font_buffers;
    free(data);
}

static unsigned g_dispatches;
static uint8_t g_action_count;
static char g_action_values[MAX_BUTTON_ACTIONS][32];
static char g_dispatched_value[32];
static const char* g_binding_value = "0";
static ActionResult g_dispatch_result = ACTION_COMPLETE;
static lv_obj_t* g_delete_on_dispatch = nullptr;

static const WidgetType* g_registered_slider = nullptr;
void widget_register(const WidgetType* type) {
    if (!strcmp(type->name, "slider")) g_registered_slider = type;
}

static OptionSelectorConfig g_selector_config{};
static bool g_selector_source_available = true;
static const char* g_selector_background = "#123456";
static const lv_image_dsc_t* g_selector_icon = nullptr;
static std::vector<std::string> g_validated_option_values;
static const PadBinding* g_pad_bindings = nullptr;
static uint8_t g_pad_binding_count = 0;

void pad_binding_set_bindings(const PadBinding* bindings, uint8_t count) {
    g_pad_bindings = bindings;
    g_pad_binding_count = bindings ? count : 0;
}
void pad_binding_get_bindings(const PadBinding** bindings, uint8_t* count) {
    if (bindings) *bindings = g_pad_bindings;
    if (count) *count = g_pad_binding_count;
}

static const lv_image_dsc_t* selector_test_icon() {
    static const uint16_t data[20 * 12] = {};
    static lv_image_dsc_t image{};
    image.header.magic = LV_IMAGE_HEADER_MAGIC;
    image.header.cf = LV_COLOR_FORMAT_RGB565;
    image.header.w = 20;
    image.header.h = 12;
    image.header.stride = 40;
    image.data_size = sizeof(data);
    image.data = reinterpret_cast<const uint8_t*>(data);
    return &image;
}

const char* option_selector_validate_config(JsonObjectConst) { return nullptr; }
bool option_selector_parse_config(const JsonObject&, uint8_t*) { return true; }
void option_selector_retain_config(uint8_t*) {}
void option_selector_release_config(uint8_t*) {}
const OptionSelectorConfig* option_selector_config(const uint8_t*) { return &g_selector_config; }
void option_selector_visit_resources(const uint8_t*, WidgetResourceVisitor, void*) {}
const char* action_validate_json(JsonObjectConst, bool, const char*, const char* value) {
    if (value) g_validated_option_values.emplace_back(value);
    return nullptr;
}
bool binding_template_resolve(const char* value, char* output, size_t size) {
    if (!strncmp(value, "[pad:test_", 10)) {
        for (uint8_t i = 0; i < g_pad_binding_count; ++i) {
            std::string token = "[pad:" + std::string(g_pad_bindings[i].name) + "]";
            if (token == value) {
                strlcpy(output, g_pad_bindings[i].value, size);
                return true;
            }
        }
        if (size) output[0] = '\0';
        return false;
    }
    strlcpy(output, !strcmp(value, "[pad:background]") ? g_selector_background : value, size);
    return true;
}
bool binding_template_has_bindings(const char* value) { return value && strchr(value, '['); }
bool device_dialog_active() { return false; }
bool device_dialog_notify(const char*, uint32_t) { return true; }
uint32_t swipe_actions_last_swipe_time() { return 0; }
bool icon_store_lookup(const char*, IconRef* ref) {
    if (!g_selector_icon) return false;
    ref->dsc = g_selector_icon;
    return true;
}
void icon_store_build_option_key(uint8_t, const char*, uint8_t, uint8_t, uint8_t, char*, size_t) {}

ActionResult action_list_dispatch_result(const ButtonAction* actions, uint8_t count,
                                         const char*, ActionContinuationOwner) {
    if (count) {
        ++g_dispatches;
        g_action_count = count;
        for (uint8_t index = 0; index < count; ++index)
            strlcpy(g_action_values[index], actions[index].payload.mqtt.mqtt_payload, sizeof(g_action_values[index]));
        strlcpy(g_dispatched_value, actions[0].payload.mqtt.mqtt_payload,
                sizeof(g_dispatched_value));
    }
    if (g_delete_on_dispatch) {
        lv_obj_t* target = g_delete_on_dispatch;
        g_delete_on_dispatch = nullptr;
        lv_obj_delete(target);
    }
    return g_dispatch_result;
}

void action_parse(const JsonObject& action, ButtonAction& output) {
    memset(&output, 0, sizeof(output));
    strlcpy(output.type, action["type"] | "", sizeof(output.type));
    strlcpy(output.payload.mqtt.mqtt_payload, action["payload"] | "",
            sizeof(output.payload.mqtt.mqtt_payload));
}

static char* test_mqtt_value_field(ButtonAction& action, size_t* size) {
    *size = sizeof(action.payload.mqtt.mqtt_payload);
    return action.payload.mqtt.mqtt_payload;
}

static bool test_mqtt_fields(ButtonAction& action, ActionFieldVisitor visitor, void* context) {
    return action_visit_field(visitor, context, "payload", action.payload.mqtt.mqtt_payload);
}

static const ActionTypeDef g_mqtt_type = {
    "mqtt", nullptr, nullptr, nullptr, test_mqtt_fields, nullptr
};

const ActionTypeDef* action_type_find(const char* type) {
    return type && strcmp(type, "mqtt") == 0 ? &g_mqtt_type : nullptr;
}

bool action_type_substitute_token(const ActionTypeDef* type,
                                  ButtonAction& action, const char* name, const char* value) {
    if (type != &g_mqtt_type) return false;
    size_t field_size = 0;
    char* field = test_mqtt_value_field(action, &field_size);
    char* token = strstr(field, name);
    if (!token) return true;
    const size_t tail = strlen(token + 7);
    const size_t replacement = strlen(value);
    if (static_cast<size_t>(token - field) + replacement + tail >= field_size)
        return false;
    memmove(token + replacement, token + 7, tail + 1);
    memcpy(token, value, replacement);
    return true;
}

struct SliderPointerInput {
    lv_point_t point{};
    bool pressed = false;
};

static void read_slider_pointer(lv_indev_t* indev, lv_indev_data_t* data) {
    const auto* input = static_cast<const SliderPointerInput*>(lv_indev_get_user_data(indev));
    data->point = input->point;
    data->state = input->pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

bool binding_template_resolve_checked(const char* source, char* output, size_t size) {
    if (!strcmp(source, "selector_source") && !g_selector_source_available) return false;
    if (!strncmp(source, "[pad:test_", 10)) return binding_template_resolve(source, output, size);
    strlcpy(output, g_binding_value, size);
    return true;
}

class SliderNativeWidget : public testing::Test {
protected:
    lv_display_t* display = nullptr;
    lv_indev_t* indev = nullptr;
    lv_obj_t* button = nullptr;
    unsigned feedback_count = 0;
    SliderPointerInput pointer{};
    WidgetConfig config{};
    WidgetState runtime{};
    ScreenButtonConfig source{};
    PadFontCache fonts;
    JsonDocument json;

    void SetUp() override {
        static bool initialized = false;
        if (!initialized) { lv_init(); initialized = true; }
        device_typography_fonts().clear();
        device_typography_init("", 0);
        g_font_loads = 0;
        g_font_buffers = 0;
        display = lv_display_create(320, 240);
        lv_display_set_default(display);
        indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(indev, display);
        lv_indev_set_user_data(indev, &pointer);
        lv_indev_set_read_cb(indev, read_slider_pointer);
        button = lv_obj_create(lv_screen_active());
        lv_obj_set_size(button, 200, 180);
        lv_obj_add_event_cb(button, [](lv_event_t* event) {
            const auto* feedback = static_cast<const lv_event_code_t*>(lv_event_get_param(event));
            if (feedback && *feedback == LV_EVENT_RELEASED)
                ++*static_cast<unsigned*>(lv_event_get_user_data(event));
        }, LV_EVENT_VALUE_CHANGED, &feedback_count);
        g_dispatches = 0;
        g_dispatched_value[0] = '\0';
        g_binding_value = "0";
        g_dispatch_result = ACTION_COMPLETE;
        pad_binding_set_bindings(nullptr, 0);
        g_delete_on_dispatch = nullptr;
        pointer = {};
        JsonObject object = json.to<JsonObject>();
        source.action_count = 1;
        strlcpy(source.actions[0].type, "mqtt", sizeof(source.actions[0].type));
        strlcpy(source.actions[0].payload.mqtt.mqtt_payload, "{value}", sizeof(source.actions[0].payload.mqtt.mqtt_payload));
        slider_parse(object, config.data);
        strlcpy(config.type, "slider", sizeof(config.type));
        runtime.font_owner = &fonts;
        slider_create(button, &config, &source, nullptr, &UI_SCALE_TABLE[0], &runtime);
        lv_obj_update_layout(button);
    }

    void TearDown() override {
        g_delete_on_dispatch = nullptr;
        slider_destroy(&runtime);
        lv_indev_delete(indev);
        lv_display_delete(display);
        fonts.clear();
        device_typography_fonts().clear();
        EXPECT_EQ(g_font_buffers, 0u);
    }

    SliderState& state() { return *reinterpret_cast<SliderState*>(runtime.data); }

    void begin(uint16_t position) {
        lv_obj_send_event(state().control, LV_EVENT_PRESSED, nullptr);
        lv_slider_set_value(state().control, position, LV_ANIM_OFF);
        lv_obj_send_event(state().control, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    void release() { lv_obj_send_event(state().control, LV_EVENT_RELEASED, nullptr); }
    void readPointer(lv_point_t point, bool pressed) {
        pointer.point = point;
        pointer.pressed = pressed;
        lv_tick_inc(10);
        lv_indev_read(indev);
    }
    void createLayout(uint8_t axis) {
        slider_destroy(&runtime);
        lv_obj_clean(button);
        runtime = WidgetState{};
        runtime.font_owner = &fonts;
        reinterpret_cast<SliderConfig*>(config.data)->axis = axis;
        slider_create(button, &config, &source, nullptr, &UI_SCALE_TABLE[0], &runtime);
        lv_obj_update_layout(button);
        slider_layout(&state());
    }
};

TEST_F(SliderNativeWidget, AutomaticSizingUsesConfiguredLengthAndShrinksToFitBothAxes) {
    auto* settings = reinterpret_cast<SliderConfig*>(config.data);
    for (uint8_t axis : {0, 1}) {
        settings->axis = axis;
        for (const auto& size : {lv_point_t{450, 300}, lv_point_t{140, 70},
                                 lv_point_t{80, 40}, lv_point_t{200, 40}}) {
            lv_obj_set_style_pad_all(button, 0, 0);
            lv_obj_set_style_border_width(button, 0, 0);
            lv_obj_set_size(button, axis == 0 ? size.x : size.y,
                            axis == 0 ? size.y : size.x);
            slider_layout(&state());
            const int target_length = size.x * settings->length_pct / 100;
            const int expected_knob = std::min(50, std::min(size.y * 3 / 5, target_length / 4));
            const int expected_track = expected_knob * 2 / 5;
            EXPECT_TRUE(state().layout_usable);
            EXPECT_EQ(axis == 0 ? lv_obj_get_width(state().control) : lv_obj_get_height(state().control),
                      target_length - expected_knob);
            EXPECT_EQ(axis == 0 ? lv_obj_get_height(state().control) : lv_obj_get_width(state().control),
                      expected_track);
            EXPECT_EQ(lv_obj_get_style_pad_left(state().control, LV_PART_KNOB),
                      (expected_knob - expected_track) / 2);
            EXPECT_EQ(lv_obj_get_x(state().control),
                      (lv_obj_get_content_width(button) - lv_obj_get_width(state().control)) / 2);
            EXPECT_EQ(lv_obj_get_y(state().control),
                      (lv_obj_get_content_height(button) - lv_obj_get_height(state().control)) / 2);
        }
    }
}

TEST_F(SliderNativeWidget, ChangedReleaseDispatchesAbsoluteValueOnce) {
    EXPECT_TRUE(state().available);
    begin(42);
    EXPECT_EQ(feedback_count, 0u);
    release();
    EXPECT_EQ(feedback_count, 1u);
    EXPECT_EQ(g_dispatches, 1u);
    EXPECT_STREQ(g_dispatched_value, "42");
    release();
    EXPECT_EQ(feedback_count, 1u);
    EXPECT_EQ(g_dispatches, 1u);
}

TEST_F(SliderNativeWidget, DragValueFloatsWithoutDispatchAndHidesOnRelease) {
    EXPECT_EQ(state().value_label, nullptr);
    begin(42);
    ASSERT_NE(state().value_label, nullptr);
    EXPECT_STREQ(lv_label_get_text(state().value_label), "42");
    EXPECT_EQ(lv_obj_get_parent(state().value_label), lv_display_get_layer_top(display));
    EXPECT_FALSE(lv_obj_has_flag(state().value_label, LV_OBJ_FLAG_CLICKABLE));
    EXPECT_FALSE(lv_obj_has_flag(state().value_label, LV_OBJ_FLAG_HIDDEN));
    EXPECT_EQ(g_dispatches, 0u);
    release();
    EXPECT_TRUE(lv_obj_has_flag(state().value_label, LV_OBJ_FLAG_HIDDEN));
    EXPECT_EQ(g_dispatches, 1u);
}

TEST_F(SliderNativeWidget, BoundVerticalSliderStaysVisibleAtZeroAfterReleaseAndPolling) {
    static uint8_t draw_buffer[320 * 240 * 3];
    std::vector<uint8_t> pixels(sizeof(draw_buffer));
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB888);
    lv_display_set_buffers(display, draw_buffer, nullptr, sizeof(draw_buffer), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_user_data(display, &pixels);
    lv_display_set_flush_cb(display, [](lv_display_t* output, const lv_area_t*, uint8_t* data) {
        auto& image = *static_cast<std::vector<uint8_t>*>(lv_display_get_user_data(output));
        std::copy(data, data + image.size(), image.begin());
        lv_display_flush_ready(output);
    });
    lv_obj_set_style_bg_color(state().control, lv_color_hex(0x00ff00), LV_PART_MAIN);
    lv_obj_set_style_bg_color(state().control, lv_color_hex(0xff00ff), LV_PART_KNOB);
    auto* settings = reinterpret_cast<SliderConfig*>(config.data);
    settings->axis = 1;
    slider_layout(&state());
    state().bound = true;
    strlcpy(config.data_binding[0], "[mock:value]", sizeof(config.data_binding[0]));
    slider_apply_report(&state(), "50", true);
    lv_refr_now(display);
    begin(0);
    EXPECT_FALSE(lv_obj_has_flag(state().control, LV_OBJ_FLAG_HIDDEN));
    release();
    EXPECT_STREQ(g_dispatched_value, "0");
    g_binding_value = "0";
    slider_tick(button, &config, &runtime);
    EXPECT_TRUE(state().available);
    EXPECT_TRUE(state().layout_usable);
    EXPECT_FALSE(state().pending);
    EXPECT_EQ(lv_slider_get_value(state().control), 0);
    EXPECT_FALSE(lv_obj_has_flag(state().control, LV_OBJ_FLAG_HIDDEN));
    EXPECT_FALSE(lv_obj_has_state(state().control, LV_STATE_DISABLED));
    lv_refr_now(display);
    unsigned track_pixels = 0;
    unsigned knob_pixels = 0;
    for (size_t offset = 0; offset < pixels.size(); offset += 3) {
        if (pixels[offset] == 0 && pixels[offset + 1] == 255 && pixels[offset + 2] == 0)
            ++track_pixels;
        if (pixels[offset] == 255 && pixels[offset + 1] == 0 && pixels[offset + 2] == 255)
            ++knob_pixels;
    }
    EXPECT_GT(track_pixels, 100u);
    EXPECT_GT(knob_pixels, 100u);
    lv_display_set_user_data(display, nullptr);
    lv_display_set_flush_cb(display, [](lv_display_t* output, const lv_area_t*, uint8_t*) {
        lv_display_flush_ready(output);
    });
}

TEST_F(SliderNativeWidget, UnavailableSourceUsesMinimumAndRemainsInteractive) {
    strlcpy(config.data_binding[0], "[mock:value]", sizeof(config.data_binding[0]));
    auto* settings = reinterpret_cast<SliderConfig*>(config.data);
    ASSERT_TRUE(slider_numeric_range_init(20, 100, 1, &settings->range));
    for (uint8_t axis : {0, 1}) {
        slider_destroy(&runtime);
        lv_obj_clean(button);
        runtime = WidgetState{};
        runtime.font_owner = &fonts;
        settings->axis = axis;
        slider_create(button, &config, &source, nullptr, &UI_SCALE_TABLE[0], &runtime);
        EXPECT_EQ(lv_slider_get_value(state().control), 0);
        EXPECT_FALSE(lv_obj_has_flag(state().control, LV_OBJ_FLAG_HIDDEN));
        EXPECT_FALSE(lv_obj_has_state(state().control, LV_STATE_DISABLED));
        slider_apply_report(&state(), "80", true);
        begin(30);
        release();
        ASSERT_TRUE(state().pending);
        const unsigned dispatches = g_dispatches;
        slider_apply_report(&state(), "null", true);
        EXPECT_FALSE(state().pending);
        EXPECT_EQ(state().display_position, 0);
        EXPECT_EQ(lv_slider_get_value(state().control), 0);
        EXPECT_FALSE(lv_obj_has_flag(state().control, LV_OBJ_FLAG_HIDDEN));
        EXPECT_FALSE(lv_obj_has_state(state().control, LV_STATE_DISABLED));
        EXPECT_EQ(g_dispatches, dispatches);
        begin(25);
        slider_apply_report(&state(), nullptr, false);
        EXPECT_TRUE(state().gesture_active);
        EXPECT_EQ(state().display_position, 25);
        release();
        EXPECT_EQ(g_dispatches, dispatches + 1);
        EXPECT_STREQ(g_dispatched_value, "45");
        slider_apply_report(&state(), nullptr, false);
        EXPECT_EQ(state().display_position, 0);
        slider_apply_report(&state(), "80", true);
        EXPECT_EQ(state().display_position, 60);
        EXPECT_EQ(g_dispatches, dispatches + 1);
    }
}

TEST_F(SliderNativeWidget, MissingActionKeepsLocalSliderInteractive) {
    state().action_count = 0;
    begin(37);
    release();
    EXPECT_EQ(g_dispatches, 0u);
    EXPECT_EQ(state().display_position, 37);
    EXPECT_FALSE(state().pending);
}

TEST_F(SliderNativeWidget, DragValueFormatsTextAndUsesNearestBitmapTypography) {
    auto* settings = reinterpret_cast<SliderConfig*>(config.data);
    ASSERT_TRUE(slider_numeric_range_init(-1, 1, 0.01, &settings->range));
    strlcpy(settings->value_format, "%s dB ", sizeof(settings->value_format));
    strlcpy(settings->value_color, "#12ab34", sizeof(settings->value_color));
    settings->value_font_size = 25;
    lv_slider_set_range(state().control, 0, settings->range.intervals);
    begin(25);
    ASSERT_NE(state().value_label, nullptr);
    EXPECT_STREQ(lv_label_get_text(state().value_label), "-0.75 dB ");
    EXPECT_EQ(lv_obj_get_style_text_font(state().value_label, LV_PART_MAIN), &lv_font_montserrat_24);
    EXPECT_EQ(lv_color_to_u32(lv_obj_get_style_text_color(state().value_label, LV_PART_MAIN)), 0xff12ab34u);
    lv_slider_set_value(state().control, 150, LV_ANIM_OFF);
    lv_obj_send_event(state().control, LV_EVENT_VALUE_CHANGED, nullptr);
    EXPECT_STREQ(lv_label_get_text(state().value_label), "0.5 dB ");
    EXPECT_EQ(g_dispatches, 0u);
    settings->value_font_size = 0;
    lv_obj_set_style_text_font(button, &lv_font_montserrat_18, 0);
    slider_update_value(&state());
    EXPECT_EQ(lv_obj_get_style_text_font(state().value_label, LV_PART_MAIN), &lv_font_montserrat_18);
}

TEST_F(SliderNativeWidget, DragValueUsesRequestedTtfSizeAndReusesPadFontCache) {
    auto* settings = reinterpret_cast<SliderConfig*>(config.data);
    settings->value_font_size = 25;
    strlcpy(source.style_center.font_name, "test-font", sizeof(source.style_center.font_name));
    createLayout(0);
    begin(42);
    const lv_font_t* font = lv_obj_get_style_text_font(state().value_label, LV_PART_MAIN);
    ASSERT_FALSE(pad_is_bitmap_font(font));
    lv_font_t* expected = lv_tiny_ttf_create_data_ex(slider_font_fixture, sizeof(slider_font_fixture),
                                                   25, LV_FONT_KERNING_NORMAL, 16);
    ASSERT_NE(expected, nullptr);
    EXPECT_EQ(font->line_height, expected->line_height);
    EXPECT_EQ(lv_font_get_glyph_width(font, '8', '8'), lv_font_get_glyph_width(expected, '8', '8'));
    lv_tiny_ttf_destroy(expected);
    EXPECT_EQ(g_font_loads, 1u);
    EXPECT_EQ(g_font_buffers, 1u);
    for (uint16_t position = 43; position <= 60; ++position) {
        lv_slider_set_value(state().control, position, LV_ANIM_OFF);
        lv_obj_send_event(state().control, LV_EVENT_VALUE_CHANGED, nullptr);
        EXPECT_EQ(lv_obj_get_style_text_font(state().value_label, LV_PART_MAIN), font);
    }
    EXPECT_EQ(g_font_loads, 1u);
    slider_hide(&runtime);
    EXPECT_EQ(g_font_buffers, 1u);
    slider_destroy(&runtime);
    EXPECT_EQ(g_font_buffers, 1u);
    fonts.clear();
    EXPECT_EQ(g_font_buffers, 0u);
}

TEST_F(SliderNativeWidget, DragValueFallsBackToNearestBitmapWhenTtfIsUnavailable) {
    auto* settings = reinterpret_cast<SliderConfig*>(config.data);
    settings->value_font_size = 25;
    strlcpy(source.style_center.font_name, "missing", sizeof(source.style_center.font_name));
    createLayout(0);
    begin(42);
    EXPECT_EQ(lv_obj_get_style_text_font(state().value_label, LV_PART_MAIN), &lv_font_montserrat_24);
    EXPECT_EQ(g_font_buffers, 0u);
}

TEST_F(SliderNativeWidget, DragBubbleFormatsDecimalsAndKeepsStableSizeWithoutChangingActions) {
    auto* settings = reinterpret_cast<SliderConfig*>(config.data);
    ASSERT_TRUE(slider_numeric_range_init(20, 30, 0.5, &settings->range));
    strlcpy(settings->value_format, "%.1f C", sizeof(settings->value_format));
    lv_slider_set_range(state().control, 0, settings->range.intervals);
    begin(6);
    ASSERT_NE(state().value_label, nullptr);
    EXPECT_STREQ(lv_label_get_text(state().value_label), "23.0 C");
    const int width = lv_obj_get_width(state().value_label);
    const int height = lv_obj_get_height(state().value_label);
    for (uint16_t position : {7, 8}) {
        lv_slider_set_value(state().control, position, LV_ANIM_OFF);
        lv_obj_send_event(state().control, LV_EVENT_VALUE_CHANGED, nullptr);
        EXPECT_EQ(lv_obj_get_width(state().value_label), width);
        EXPECT_EQ(lv_obj_get_height(state().value_label), height);
    }
    EXPECT_STREQ(lv_label_get_text(state().value_label), "24.0 C");
    EXPECT_EQ(lv_obj_get_style_bg_opa(state().value_label, LV_PART_MAIN), LV_OPA_COVER);
    EXPECT_GT(lv_obj_get_style_radius(state().value_label, LV_PART_MAIN), 0);
    for (const char* color : {"#ffffff", "#000000"}) {
        strlcpy(settings->value_color, color, sizeof(settings->value_color));
        slider_update_value(&state());
        const auto foreground = lv_obj_get_style_text_color(state().value_label, LV_PART_MAIN);
        const auto background = lv_obj_get_style_bg_color(state().value_label, LV_PART_MAIN);
        EXPECT_GT(std::abs(static_cast<int>(lv_color_brightness(foreground)) - lv_color_brightness(background)), 200);
    }
    EXPECT_EQ(g_dispatches, 0u);
    release();
    EXPECT_EQ(g_dispatches, 1u);
    EXPECT_STREQ(g_dispatched_value, "24");
    strlcpy(settings->value_format, "%.0f%%", sizeof(settings->value_format));
    begin(10);
    EXPECT_STREQ(lv_label_get_text(state().value_label), "25%");
}

TEST_F(SliderNativeWidget, DragBubbleReservesAutomaticFractionWidthAndRespectsKnobClearance) {
    auto* settings = reinterpret_cast<SliderConfig*>(config.data);
    ASSERT_TRUE(slider_numeric_range_init(-1, 1, 0.01, &settings->range));
    lv_slider_set_range(state().control, 0, settings->range.intervals);
    lv_obj_set_size(button, 200, 180);
    lv_obj_set_pos(button, 20, 30);
    for (uint8_t axis : {0, 1}) {
        settings->axis = axis;
        for (uint16_t clearance : {10, 40}) {
            settings->value_clearance = clearance;
            slider_layout(&state());
            begin(100);
            EXPECT_STREQ(lv_label_get_text(state().value_label), "0");
            const int width = lv_obj_get_width(state().value_label);
            lv_slider_set_value(state().control, 25, LV_ANIM_OFF);
            lv_obj_send_event(state().control, LV_EVENT_VALUE_CHANGED, nullptr);
            EXPECT_STREQ(lv_label_get_text(state().value_label), "-0.75");
            EXPECT_EQ(lv_obj_get_width(state().value_label), width);
            lv_area_t track, bubble;
            lv_obj_get_content_coords(state().control, &track);
            lv_obj_get_coords(state().value_label, &bubble);
            const int knob = lv_obj_get_style_pad_left(state().control, LV_PART_KNOB);
            if (axis == 0) { EXPECT_EQ(track.y1 - knob - bubble.y2 - 1, clearance); }
            else { EXPECT_EQ(bubble.x1 - track.x2 - knob - 1, clearance); }
            EXPECT_LE(lv_obj_get_width(state().value_label), 100);
            slider_cancel_input(&runtime);
        }
    }
}

TEST_F(SliderNativeWidget, DragValueStaysOnScreenAndDoesNotChangeLayout) {
    auto* settings = reinterpret_cast<SliderConfig*>(config.data);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    for (uint8_t axis : {0, 1}) {
        settings->axis = axis;
        lv_obj_set_size(button, axis == 0 ? 180 : 60, axis == 0 ? 60 : 180);
        for (const lv_point_t origin : {lv_point_t{0, 0}, lv_point_t{140, 60}}) {
            lv_obj_set_pos(button, origin.x, origin.y);
            slider_layout(&state());
            lv_area_t before;
            lv_obj_get_coords(state().control, &before);
            begin(50);
            ASSERT_NE(state().value_label, nullptr);
            lv_obj_update_layout(state().value_label);
            lv_area_t label, after;
            lv_obj_get_coords(state().value_label, &label);
            lv_obj_get_coords(state().control, &after);
            EXPECT_GE(label.x1, 0); EXPECT_GE(label.y1, 0);
            EXPECT_LT(label.x2, 320); EXPECT_LT(label.y2, 240);
            EXPECT_EQ(before.x1, after.x1); EXPECT_EQ(before.y1, after.y1);
            EXPECT_EQ(before.x2, after.x2); EXPECT_EQ(before.y2, after.y2);
            if (axis == 0 && origin.y == 0) { EXPECT_GT(label.y1, before.y2); }
            if (axis == 1 && origin.x == 0) { EXPECT_GT(label.x1, before.x2); }
            slider_cancel_input(&runtime);
            EXPECT_TRUE(lv_obj_has_flag(state().value_label, LV_OBJ_FLAG_HIDDEN));
        }
    }
}

TEST_F(SliderNativeWidget, DragValueHonorsDisabledSettingAndCleansUpOnHideAndDestroy) {
    auto* settings = reinterpret_cast<SliderConfig*>(config.data);
    settings->show_value = false;
    begin(40);
    EXPECT_EQ(state().value_label, nullptr);
    slider_cancel_input(&runtime);
    settings->show_value = true;
    lv_obj_send_event(state().control, LV_EVENT_PRESSED, nullptr);
    EXPECT_EQ(state().value_label, nullptr);
    begin(65);
    ASSERT_NE(state().value_label, nullptr);
    slider_hide(&runtime);
    EXPECT_TRUE(lv_obj_has_flag(state().value_label, LV_OBJ_FLAG_HIDDEN));
    EXPECT_EQ(g_dispatches, 0u);
    const uint32_t children = lv_obj_get_child_count(lv_display_get_layer_top(display));
    slider_destroy(&runtime);
    EXPECT_EQ(state().value_label, nullptr);
    EXPECT_EQ(lv_obj_get_child_count(lv_display_get_layer_top(display)), children - 1);
}

TEST_F(SliderNativeWidget, MissingActionBoundSliderReturnsToReportedValue) {
    strlcpy(config.data_binding[0], "[mock:value]", sizeof(config.data_binding[0]));
    slider_destroy(&runtime);
    lv_obj_clean(button);
    runtime = WidgetState{};
    auto* slider_config = reinterpret_cast<SliderConfig*>(config.data);
    ASSERT_TRUE(slider_numeric_range_init(0, 1, 0.01, &slider_config->range));
    source.action_count = 0;
    slider_create(button, &config, &source, nullptr, &UI_SCALE_TABLE[0], &runtime);
    g_binding_value = "0.2";
    slider_tick(button, &config, &runtime);
    ASSERT_EQ(state().display_position, 20);
    begin(60);
    release();
    EXPECT_EQ(g_dispatches, 0u);
    EXPECT_EQ(state().display_position, 20);
}

TEST_F(SliderNativeWidget, PointerDragAndTrackTapDispatchOnRelease) {
    unsigned swipes = 0;
    lv_obj_add_event_cb(lv_screen_active(), [](lv_event_t* event) {
        ++*static_cast<unsigned*>(lv_event_get_user_data(event));
        lv_indev_wait_release(lv_indev_active());
    }, LV_EVENT_GESTURE, &swipes);
    lv_area_t area{};
    lv_obj_get_coords(state().control, &area);
    const int center_y = (area.y1 + area.y2) / 2;
    readPointer({area.x1 + 4, center_y}, true);
    readPointer({area.x1 + (area.x2 - area.x1) * 3 / 4, center_y}, true);
    EXPECT_TRUE(state().gesture_active);
    ASSERT_NE(state().value_label, nullptr);
    EXPECT_FALSE(lv_obj_has_flag(state().value_label, LV_OBJ_FLAG_HIDDEN));
    EXPECT_EQ(feedback_count, 0u);
    readPointer({area.x1 + (area.x2 - area.x1) * 3 / 4, center_y}, false);
    EXPECT_EQ(feedback_count, 1u);
    EXPECT_EQ(g_dispatches, 1u);
    EXPECT_GT(std::atoi(g_dispatched_value), 50);
    EXPECT_TRUE(lv_obj_has_flag(state().value_label, LV_OBJ_FLAG_HIDDEN));

    lv_obj_get_coords(state().control, &area);
    readPointer({area.x1 + (area.x2 - area.x1) / 3, center_y}, true);
    EXPECT_FALSE(lv_obj_has_flag(state().value_label, LV_OBJ_FLAG_HIDDEN));
    readPointer({area.x1 + (area.x2 - area.x1) / 3, center_y}, false);
    EXPECT_EQ(feedback_count, 2u);
    EXPECT_EQ(g_dispatches, 2u);
    EXPECT_EQ(swipes, 0u);
}

TEST_F(SliderNativeWidget, InsufficientContentHidesControlWithoutInvalidGeometry) {
    lv_obj_set_size(button, 10, 10);
    createLayout(0);
    const auto* layout = reinterpret_cast<const SliderState*>(runtime.data);
    EXPECT_FALSE(layout->layout_usable);
    EXPECT_TRUE(lv_obj_has_flag(layout->control, LV_OBJ_FLAG_HIDDEN));
    EXPECT_GE(lv_obj_get_width(layout->control), 1);
    EXPECT_GE(lv_obj_get_height(layout->control), 1);
}

TEST_F(SliderNativeWidget, ImmediateDispatchFailureRestoresGestureStart) {
    g_dispatch_result = ACTION_FAILED;
    begin(30);
    release();
    EXPECT_EQ(g_dispatches, 1u);
    EXPECT_EQ(state().display_position, 0);
    EXPECT_FALSE(state().pending);
}

TEST_F(SliderNativeWidget, ThreeActionsShareReleaseSnapshotAndAllowLiteralActions) {
    source.actions[1] = source.actions[0];
    source.actions[2] = source.actions[0];
    strlcpy(source.actions[1].payload.mqtt.mqtt_payload, "literal", sizeof(source.actions[1].payload.mqtt.mqtt_payload));
    state().action_count = 3;
    begin(43);
    release();
    EXPECT_EQ(g_dispatches, 1u);
    EXPECT_EQ(g_action_count, 3u);
    EXPECT_STREQ(g_action_values[0], "43");
    EXPECT_STREQ(g_action_values[1], "literal");
    EXPECT_STREQ(g_action_values[2], "43");
    EXPECT_STREQ(source.actions[0].payload.mqtt.mqtt_payload, "{value}");
}

TEST_F(SliderNativeWidget, LengthAndThicknessFollowConfiguration) {
    createLayout(0);
    auto* settings = reinterpret_cast<SliderConfig*>(config.data);
    settings->track_px = 4;
    settings->knob_px = 20;
    slider_layout(&state());
    EXPECT_EQ(lv_obj_get_height(state().control), 4);
    EXPECT_EQ(lv_obj_get_style_pad_left(state().control, LV_PART_KNOB), 8);
    settings->length_pct = 75;
    slider_layout(&state());
    EXPECT_EQ(lv_obj_get_width(state().control) + 20, lv_obj_get_content_width(button) * 75 / 100);
}

TEST_F(SliderNativeWidget, PreprocessedPermissionGuardPreventsNativePress) {
    slider_attach(&runtime, source.actions, source.action_count, [](lv_event_t* event) {
        if (lv_event_get_code(event) == LV_EVENT_PRESSED) lv_event_stop_processing(event);
    }, nullptr);
    lv_area_t area{};
    lv_obj_get_coords(state().control, &area);
    readPointer({area.x2, (area.y1 + area.y2) / 2}, true);
    readPointer({area.x1, (area.y1 + area.y2) / 2}, false);
    EXPECT_FALSE(state().gesture_active);
    EXPECT_EQ(g_dispatches, 0u);
    EXPECT_EQ(lv_slider_get_value(state().control), 0);
}

TEST_F(SliderNativeWidget, ActionMayDestroyOwningButtonSynchronously) {
    g_delete_on_dispatch = button;
    begin(30);
    release();
    EXPECT_EQ(g_dispatches, 1u);
    EXPECT_EQ(g_delete_on_dispatch, nullptr);
}

TEST_F(SliderNativeWidget, UnchangedReleaseAndPressLossDoNotDispatch) {
    ASSERT_TRUE(widget_owns_input(g_registered_slider));
    EXPECT_FALSE(widget_owns_input(nullptr));
    begin(40);
    widget_cancel_input(g_registered_slider, &runtime);
    release();
    EXPECT_EQ(g_dispatches, 0u);
    EXPECT_EQ(state().display_position, 0);
    begin(0);
    release();
    EXPECT_EQ(g_dispatches, 0u);
    begin(65);
    lv_obj_send_event(state().control, LV_EVENT_PRESS_LOST, nullptr);
    EXPECT_EQ(g_dispatches, 0u);
    EXPECT_EQ(feedback_count, 0u);
    EXPECT_EQ(state().display_position, 0);
}

TEST_F(SliderNativeWidget, BoundReportsDoNotDispatchAndPendingExpires) {
    json["widget_slider_min"] = 0;
    json["widget_slider_max"] = 1;
    json["widget_slider_step"] = 0.01;
    strlcpy(config.data_binding[0], "[mock:value]", sizeof(config.data_binding[0]));
    slider_destroy(&runtime);
    lv_obj_clean(button);
    runtime = WidgetState{};
    auto* slider_config = reinterpret_cast<SliderConfig*>(config.data);
    ASSERT_TRUE(slider_numeric_range_init(0, 1, 0.01, &slider_config->range));
    slider_create(button, &config, &source, nullptr, &UI_SCALE_TABLE[0], &runtime);
    g_binding_value = "0";
    slider_tick(button, &config, &runtime);
    EXPECT_TRUE(state().available);
    EXPECT_EQ(state().display_position, 0);
    EXPECT_EQ(g_dispatches, 0u);
    EXPECT_EQ(feedback_count, 0u);

    begin(25);
    EXPECT_TRUE(state().gesture_active);
    EXPECT_EQ(lv_slider_get_value(state().control), 25);
    g_binding_value = "0.2";
    slider_tick(button, &config, &runtime);
    EXPECT_TRUE(state().gesture_active);
    EXPECT_EQ(lv_slider_get_value(state().control), 25);
    EXPECT_EQ(state().draft_position, 25);
    EXPECT_EQ(state().display_position, 25);
    EXPECT_EQ(state().gesture_start_position, 0);
    release();
    EXPECT_EQ(g_dispatches, 1u);
    EXPECT_STREQ(g_dispatched_value, "0.25");
    EXPECT_TRUE(state().pending);

    begin(40);
    g_binding_value = "0.3";
    slider_tick(button, &config, &runtime);
    EXPECT_EQ(state().display_position, 40);
    release();
    EXPECT_EQ(g_dispatches, 2u);
    EXPECT_STREQ(g_dispatched_value, "0.4");
    EXPECT_EQ(state().pending_position, 40);

    begin(60);
    lv_obj_send_event(state().control, LV_EVENT_PRESS_LOST, nullptr);
    EXPECT_EQ(state().display_position, 40);
    EXPECT_TRUE(state().pending);

    lv_tick_inc(5000);
    slider_tick(button, &config, &runtime);
    EXPECT_FALSE(state().pending);
    EXPECT_EQ(state().display_position, 30);
}

TEST(SliderSchema, RejectsInvalidRangesActionsAndUnsupportedFields) {
    JsonDocument document;
    JsonObject button = document.to<JsonObject>();
    EXPECT_EQ(slider_validate(button), nullptr);
    button["actions"].to<JsonArray>().add<JsonObject>()["type"] = "mqtt";
    EXPECT_EQ(slider_validate(button), nullptr);

    button["widget_slider_min"] = 0;
    button["widget_slider_max"] = 1;
    button["widget_slider_step"] = 0.3;
    EXPECT_NE(slider_validate(button), nullptr);
    button["widget_slider_step"] = 0.01;
    EXPECT_EQ(slider_validate(button), nullptr);

    button["lp_actions"].to<JsonArray>().add<JsonObject>()["type"] = "mqtt";
    EXPECT_NE(slider_validate(button), nullptr);
    button.remove("lp_actions");
    button["widget_slider_knob_px"] = 4;
    EXPECT_NE(slider_validate(button), nullptr);
    button.remove("widget_slider_knob_px");
    button["widget_slider_ticks"] = true;
    EXPECT_NE(slider_validate(button), nullptr);
    button.remove("widget_slider_ticks");
    button["widget_slider_show_value"] = false;
    button["widget_slider_value_font_size"] = 24;
    button["widget_slider_value_color"] = "";
    button["widget_slider_value_side"] = "before";
    EXPECT_EQ(slider_validate(button), nullptr);
    WidgetConfig parsed{};
    slider_parse(button, parsed.data);
    const auto* settings = reinterpret_cast<const SliderConfig*>(parsed.data);
    EXPECT_EQ(settings->length_pct, 100);
    button["widget_slider_length_pct"] = 75;
    slider_parse(button, parsed.data);
    EXPECT_EQ(settings->length_pct, 75);
    EXPECT_FALSE(settings->show_value);
    EXPECT_EQ(settings->value_font_size, 24);
    button["widget_slider_value_font_size"] = 65560;
    EXPECT_NE(slider_validate(button), nullptr);
    button["widget_slider_value_font_size"] = 0;
    for (unsigned size : {0u, 25u, 512u}) {
        button["widget_slider_value_font_size"] = size;
        EXPECT_EQ(slider_validate(button), nullptr);
        slider_parse(button, parsed.data);
        EXPECT_EQ(settings->value_font_size, size);
    }
    for (const char* format : {"%n", "%p", "%*f", "%f %f", "%.9f", "%33f", "%+s", "trailing%", "plain text"}) {
        button["widget_slider_value_format"] = format;
        EXPECT_NE(slider_validate(button), nullptr) << format;
    }
    for (const char* format : {"%.1f", "%.0f%%", " %s dB ", "%08.2f", "%.8Lf", ""}) {
        button["widget_slider_value_format"] = format;
        EXPECT_EQ(slider_validate(button), nullptr) << format;
    }
    button["widget_slider_value_format"] = std::string(65, 'a');
    EXPECT_NE(slider_validate(button), nullptr);
    button["widget_slider_value_format"] = " %.1f C ";
    button["widget_slider_value_clearance"] = 60;
    EXPECT_EQ(slider_validate(button), nullptr);
    slider_parse(button, parsed.data);
    EXPECT_STREQ(settings->value_format, " %.1f C ");
    EXPECT_EQ(settings->value_clearance, 60);
    for (double clearance : {-1.0, 0.5, 201.0}) {
        button["widget_slider_value_clearance"] = clearance;
        EXPECT_NE(slider_validate(button), nullptr);
    }
}

TEST(SliderNumeric, DefaultAndNegativeRanges) {
    SliderNumericRange range{};
    ASSERT_TRUE(slider_numeric_range_init(0, 100, 1, &range));
    EXPECT_EQ(range.intervals, 100);
    EXPECT_EQ(slider_numeric_value_at(range, range.intervals), 100 * SLIDER_FIXED_SCALE);

    ASSERT_TRUE(slider_numeric_range_init(-10, 10, 0.5, &range));
    EXPECT_EQ(range.intervals, 40);
    EXPECT_EQ(slider_numeric_value_at(range, 0), -10 * SLIDER_FIXED_SCALE);
}

TEST(SliderNumeric, DecimalStepSurvivesBinaryFloatingPoint) {
    SliderNumericRange range{};
    ASSERT_TRUE(slider_numeric_range_init(0, 1, 0.01, &range));
    EXPECT_EQ(range.intervals, 100);
    EXPECT_EQ(slider_numeric_value_at(range, 1), 100);
    EXPECT_EQ(slider_numeric_value_at(range, 100), SLIDER_FIXED_SCALE);
}

TEST(SliderNumeric, ClampsEndpointsAndRoundsTiesTowardLargerValue) {
    SliderNumericRange range{};
    ASSERT_TRUE(slider_numeric_range_init(-1, 1, 0.5, &range));
    EXPECT_EQ(slider_numeric_position_for(range, -2 * SLIDER_FIXED_SCALE), 0);
    EXPECT_EQ(slider_numeric_position_for(range, 2 * SLIDER_FIXED_SCALE), range.intervals);
    EXPECT_EQ(slider_numeric_position_for(range, -2500), 2);
    EXPECT_EQ(slider_numeric_position_for(range, 2500), 3);
}

TEST(SliderNumeric, ParsesSourcesStrictlyAndClampsValidValues) {
    SliderNumericRange range{};
    ASSERT_TRUE(slider_numeric_range_init(-1, 1, 0.01, &range));
    int64_t value = -1;
    ASSERT_TRUE(slider_numeric_parse_source("0", range, &value));
    EXPECT_EQ(value, 0);
    ASSERT_TRUE(slider_numeric_parse_source("1.25", range, &value));
    EXPECT_EQ(value, SLIDER_FIXED_SCALE);
    ASSERT_TRUE(slider_numeric_parse_source("-2", range, &value));
    EXPECT_EQ(value, -SLIDER_FIXED_SCALE);
    EXPECT_FALSE(slider_numeric_parse_source("", range, &value));
    EXPECT_FALSE(slider_numeric_parse_source("0.2tail", range, &value));
    EXPECT_FALSE(slider_numeric_parse_source("0x10", range, &value));
    EXPECT_FALSE(slider_numeric_parse_source("nan", range, &value));
    EXPECT_FALSE(slider_numeric_parse_source("inf", range, &value));
    EXPECT_FALSE(slider_numeric_parse_source("0.00001", range, &value));
}

TEST(SliderNumeric, RejectsUnsupportedPrecisionAndInvalidRanges) {
    SliderNumericRange range{};
    EXPECT_FALSE(slider_numeric_range_init(0, 1, 0.00001, &range));
    EXPECT_FALSE(slider_numeric_range_init(0, 1, 0.3, &range));
    EXPECT_FALSE(slider_numeric_range_init(1, 1, 1, &range));
    EXPECT_FALSE(slider_numeric_range_init(0, 10001, 1, &range));
    EXPECT_FALSE(slider_numeric_range_init(1000000.0001, 1000001, 1, &range));
    EXPECT_TRUE(slider_numeric_range_init(-1000000, 1000000, 2000000, &range));
    EXPECT_EQ(range.intervals, 1);
}

TEST(SliderNumeric, FormatsWithoutExponentOrTrailingZeros) {
    char output[24];
    ASSERT_TRUE(slider_numeric_format(0, output, sizeof(output)));
    EXPECT_STREQ(output, "0");
    ASSERT_TRUE(slider_numeric_format(-123450, output, sizeof(output)));
    EXPECT_STREQ(output, "-12.345");
    ASSERT_TRUE(slider_numeric_format(10000, output, sizeof(output)));
    EXPECT_STREQ(output, "1");
    ASSERT_TRUE(slider_numeric_format(1, output, sizeof(output)));
    EXPECT_STREQ(output, "0.0001");
    std::strcpy(output, "unchanged");
    EXPECT_FALSE(slider_numeric_format(12345, output, 5));
    EXPECT_STREQ(output, "unchanged");
}

class OptionSelectorNativeWidget : public testing::Test {
protected:
    lv_display_t* display = nullptr;
    lv_indev_t* indev = nullptr;
    lv_obj_t* button = nullptr;
    SliderPointerInput pointer{};
    WidgetConfig config{};
    WidgetState runtime{};
    ScreenButtonConfig source{};
    PadFontCache fonts;
    PadBinding named_bindings[3]{};
    uint8_t named_binding_count = 0;
    std::vector<lv_event_code_t> feedback_events;

    void SetUp() override {
        lv_init();
        display = lv_display_create(320, 240);
        lv_display_set_default(display);
        indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(indev, display);
        lv_indev_set_user_data(indev, &pointer);
        lv_indev_set_read_cb(indev, read_slider_pointer);
        button = lv_obj_create(lv_screen_active());
        lv_obj_set_size(button, 200, 80);
        lv_obj_add_event_cb(lv_screen_active(), [](lv_event_t* event) {
            auto* self = static_cast<OptionSelectorNativeWidget*>(lv_event_get_user_data(event));
            const auto* feedback = static_cast<const OptionSelectorFeedback*>(lv_event_get_param(event));
            self->feedback_events.push_back(feedback->event);
        }, LV_EVENT_VALUE_CHANGED, this);
        g_selector_config = {};
        g_selector_icon = nullptr;
        for (unsigned index = 0; index < 2; ++index) {
            auto& slot = g_selector_config.slots[index];
            slot.kind = OptionSelectorContent::Label;
            strlcpy(slot.value, index ? "heat" : "cool", sizeof(slot.value));
            strlcpy(slot.content.label, slot.value, sizeof(slot.content.label));
        }
        strlcpy(config.data_binding[0], "selector_source", sizeof(config.data_binding[0]));
        strlcpy(source.actions[0].type, "mqtt", sizeof(source.actions[0].type));
        strlcpy(source.actions[0].payload.mqtt.mqtt_payload, "{value}",
                sizeof(source.actions[0].payload.mqtt.mqtt_payload));
        runtime.font_owner = &fonts;
        g_binding_value = "cool";
        g_selector_source_available = true;
        g_dispatches = 0;
        g_dispatch_result = ACTION_COMPLETE;
        pad_binding_set_bindings(nullptr, 0);
    }

    void create(OptionSelectorMode mode, uint8_t action_count = 1) {
        g_selector_config.mode = mode;
        option_selector_widget_impl::create(button, &config, &source, nullptr, &UI_SCALE_TABLE[0], &runtime);
        option_selector_attach(&runtime, source.actions, action_count, 0, false, 0, 0, nullptr, nullptr,
                               named_bindings, named_binding_count);
    }

    option_selector_widget_impl::State* state() {
        return reinterpret_cast<option_selector_widget_impl::State*>(runtime.data);
    }

    void TearDown() override {
        pad_binding_set_bindings(nullptr, 0);
        option_selector_widget_impl::destroy(&runtime);
        lv_indev_delete(indev);
        lv_display_delete(display);
        fonts.clear();
    }
};

TEST_F(OptionSelectorNativeWidget, ZeroWidthOptionsStayHiddenAcrossResizes) {
    auto& third = g_selector_config.slots[2];
    third.kind = OptionSelectorContent::Label;
    strlcpy(third.content.label, "third", sizeof(third.content.label));
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_size(button, 2, 20);
    create(OptionSelectorMode::None);
    const auto check_narrow = [&]() {
        lv_obj_update_layout(button);
        option_selector_widget_impl::layout(state());
        option_selector_widget_impl::render(state());
        lv_obj_update_layout(button);
        unsigned visible = 0;
        lv_area_t previous{};
        for (auto& slot : state()->slots) {
            if (!slot.hit) continue;
            if (!slot.shown) {
                EXPECT_TRUE(lv_obj_has_flag(slot.hit, LV_OBJ_FLAG_HIDDEN));
                option_selector_widget_impl::activate(state(), &slot - state()->slots);
            } else {
                lv_area_t bounds{};
                lv_obj_get_coords(slot.hit, &bounds);
                EXPECT_GE(bounds.x2, bounds.x1);
                if (visible) { EXPECT_GT(bounds.x1, previous.x2); }
                previous = bounds;
                ++visible;
            }
        }
        EXPECT_EQ(visible, 2u);
        EXPECT_EQ(g_dispatches, 0u);
    };
    check_narrow();
    lv_obj_set_size(button, 30, 20);
    option_selector_widget_impl::layout(state());
    option_selector_widget_impl::render(state());
    for (unsigned i = 0; i < 3; ++i) EXPECT_TRUE(state()->slots[i].shown);
    lv_obj_set_size(button, 2, 20);
    check_narrow();
    lv_obj_set_size(button, 30, 20);
    option_selector_widget_impl::layout(state());
    option_selector_widget_impl::render(state());
    for (unsigned i = 0; i < 3; ++i) EXPECT_TRUE(state()->slots[i].shown);
}

TEST_F(OptionSelectorNativeWidget, EventRenderUsesAndRestoresPadBindings) {
    strlcpy(named_bindings[0].name, "test_label", sizeof(named_bindings[0].name));
    strlcpy(named_bindings[0].value, "Own label", sizeof(named_bindings[0].value));
    strlcpy(named_bindings[1].name, "test_state", sizeof(named_bindings[1].name));
    strlcpy(named_bindings[1].value, "enabled", sizeof(named_bindings[1].value));
    strlcpy(named_bindings[2].name, "test_color", sizeof(named_bindings[2].name));
    strlcpy(named_bindings[2].value, "#abcdef", sizeof(named_bindings[2].value));
    named_binding_count = 3;
    strlcpy(g_selector_config.slots[1].content.label, "[pad:test_label]",
            sizeof(g_selector_config.slots[1].content.label));
    strlcpy(g_selector_config.slots[1].state, "[pad:test_state]", sizeof(g_selector_config.slots[1].state));
    strlcpy(g_selector_config.slots[1].foreground, "[pad:test_color]",
            sizeof(g_selector_config.slots[1].foreground));
    create(OptionSelectorMode::Local);
    PadBinding other{};
    strlcpy(other.name, "test_label", sizeof(other.name));
    strlcpy(other.value, "Wrong label", sizeof(other.value));
    pad_binding_set_bindings(&other, 1);
    option_selector_widget_impl::activate(state(), 1);
    EXPECT_EQ(g_dispatches, 1u);
    EXPECT_EQ(state()->selected, 1);
    EXPECT_STREQ(lv_label_get_text(state()->slots[1].content), "Own label");
    EXPECT_EQ(state()->slots[1].state, BTN_STATE_ENABLED);
    EXPECT_EQ(state()->slots[1].foreground, 0xabcdefu);
    EXPECT_EQ(g_pad_bindings, &other);
    EXPECT_EQ(g_pad_binding_count, 1);
}

TEST_F(OptionSelectorNativeWidget, PointerTapDispatchesTheChosenOption) {
    create(OptionSelectorMode::None);
    lv_obj_update_layout(button);
    lv_area_t bounds{};
    lv_obj_get_coords(state()->slots[1].hit, &bounds);
    pointer.point = {(bounds.x1 + bounds.x2) / 2, (bounds.y1 + bounds.y2) / 2};
    lv_tick_inc(400);
    pointer.pressed = true;
    lv_indev_read(indev);
    EXPECT_EQ(feedback_events, std::vector<lv_event_code_t>({LV_EVENT_PRESSED}));
    EXPECT_FALSE(state()->slots[1].background_visible);
    pointer.pressed = false;
    lv_tick_inc(10);
    lv_indev_read(indev);
    EXPECT_EQ(g_dispatches, 1u);
    EXPECT_STREQ(g_dispatched_value, "heat");
    EXPECT_EQ(feedback_events, std::vector<lv_event_code_t>(
        {LV_EVENT_PRESSED, LV_EVENT_RELEASED, LV_EVENT_CLICKED}));
}

TEST_F(OptionSelectorNativeWidget, LocalSelectionWithoutActionsHasNoPressFeedback) {
    create(OptionSelectorMode::Local, 0);
    lv_obj_update_layout(button);
    lv_area_t bounds{};
    lv_obj_get_coords(state()->slots[1].hit, &bounds);
    pointer.point = {(bounds.x1 + bounds.x2) / 2, (bounds.y1 + bounds.y2) / 2};
    lv_tick_inc(400);
    pointer.pressed = true;
    lv_indev_read(indev);
    EXPECT_TRUE(feedback_events.empty());
    EXPECT_FALSE(state()->slots[1].background_visible);
    pointer.pressed = false;
    lv_tick_inc(10);
    lv_indev_read(indev);
    EXPECT_EQ(state()->selected, 1);
    EXPECT_TRUE(state()->slots[1].background_visible);
    EXPECT_EQ(g_dispatches, 0u);
    EXPECT_EQ(feedback_events, std::vector<lv_event_code_t>({LV_EVENT_CLICKED}));

    feedback_events.clear();
    pointer.pressed = true;
    lv_tick_inc(400);
    lv_indev_read(indev);
    pointer.pressed = false;
    lv_tick_inc(10);
    lv_indev_read(indev);
    EXPECT_TRUE(feedback_events.empty());
}

TEST_F(OptionSelectorNativeWidget, BoundSelectedOptionHasNoFeedback) {
    create(OptionSelectorMode::Bound);
    option_selector_widget_impl::tick(button, &config, &runtime);
    lv_obj_update_layout(button);
    lv_area_t bounds{};
    lv_obj_get_coords(state()->slots[0].hit, &bounds);
    pointer.point = {(bounds.x1 + bounds.x2) / 2, (bounds.y1 + bounds.y2) / 2};
    lv_tick_inc(400);
    pointer.pressed = true;
    lv_indev_read(indev);
    pointer.pressed = false;
    lv_tick_inc(10);
    lv_indev_read(indev);
    EXPECT_TRUE(feedback_events.empty());
    EXPECT_EQ(g_dispatches, 0u);
}

TEST_F(OptionSelectorNativeWidget, BoundWithoutActionsHasNoFeedback) {
    create(OptionSelectorMode::Bound, 0);
    option_selector_widget_impl::tick(button, &config, &runtime);
    lv_obj_update_layout(button);
    lv_area_t bounds{};
    lv_obj_get_coords(state()->slots[1].hit, &bounds);
    pointer.point = {(bounds.x1 + bounds.x2) / 2, (bounds.y1 + bounds.y2) / 2};
    lv_tick_inc(400);
    pointer.pressed = true;
    lv_indev_read(indev);
    pointer.pressed = false;
    lv_tick_inc(10);
    lv_indev_read(indev);
    EXPECT_TRUE(feedback_events.empty());
    EXPECT_EQ(g_dispatches, 0u);
}

TEST_F(OptionSelectorNativeWidget, BecomingUnavailableCancelsPressFeedback) {
    create(OptionSelectorMode::Bound);
    option_selector_widget_impl::tick(button, &config, &runtime);
    lv_obj_update_layout(button);
    lv_area_t bounds{};
    lv_obj_get_coords(state()->slots[1].hit, &bounds);
    pointer.point = {(bounds.x1 + bounds.x2) / 2, (bounds.y1 + bounds.y2) / 2};
    lv_tick_inc(400);
    pointer.pressed = true;
    lv_indev_read(indev);
    g_selector_source_available = false;
    option_selector_widget_impl::tick(button, &config, &runtime);
    pointer.pressed = false;
    lv_tick_inc(10);
    lv_indev_read(indev);
    EXPECT_EQ(feedback_events, std::vector<lv_event_code_t>(
        {LV_EVENT_PRESSED, LV_EVENT_PRESS_LOST}));
    EXPECT_EQ(g_dispatches, 0u);
}

TEST_F(OptionSelectorNativeWidget, InsetChangesBadgesWithoutShrinkingTouchTargets) {
    g_selector_config.option_inset = 6;
    g_selector_config.slots[1].kind = OptionSelectorContent::Icon;
    create(OptionSelectorMode::None);
    lv_obj_update_layout(button);
    auto& slot = state()->slots[0];
    EXPECT_EQ(lv_obj_get_width(slot.badge), lv_obj_get_width(slot.hit) - 12);
    EXPECT_EQ(lv_obj_get_height(slot.badge), lv_obj_get_height(slot.hit) - 12);
    auto& icon = state()->slots[1];
    EXPECT_EQ(lv_obj_get_width(icon.badge), lv_obj_get_width(icon.hit) - 12);
    EXPECT_EQ(lv_obj_get_height(icon.badge), lv_obj_get_height(icon.hit) - 12);
    lv_area_t bounds{};
    lv_obj_get_coords(slot.hit, &bounds);
    pointer.point = {bounds.x1 + 1, (bounds.y1 + bounds.y2) / 2};
    lv_tick_inc(400);
    pointer.pressed = true;
    lv_indev_read(indev);
    pointer.pressed = false;
    lv_tick_inc(10);
    lv_indev_read(indev);
    EXPECT_EQ(g_dispatches, 1u);
    EXPECT_STREQ(g_dispatched_value, "cool");

    g_selector_config.option_inset = OPTION_SELECTOR_MAX_INSET;
    lv_obj_set_size(button, 30, 20);
    option_selector_widget_impl::layout(state());
    lv_obj_update_layout(button);
    EXPECT_GE(lv_obj_get_width(slot.badge), 1);
    EXPECT_GE(lv_obj_get_height(slot.badge), 1);
}

TEST_F(OptionSelectorNativeWidget, IconAndTextUseSamePillWithoutResizingIcon) {
    g_selector_icon = selector_test_icon();
    g_selector_config.slots[1] = {};
    g_selector_config.slots[1].kind = OptionSelectorContent::Icon;
    g_selector_config.slots[1].content.icon.mono = true;
    strlcpy(g_selector_config.slots[1].value, "heat", sizeof(g_selector_config.slots[1].value));
    strlcpy(g_selector_config.selected_foreground, "#FFFFFF", sizeof(g_selector_config.selected_foreground));
    g_selector_config.option_inset = 4;
    g_selector_config.icon_badge_padding = 4;
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_text_color(button, lv_color_hex(0x395059), 0);
    lv_obj_set_size(button, 240, 100);
    create(OptionSelectorMode::Local);
    lv_obj_t* icon_content = state()->slots[1].content;
    EXPECT_EQ(lv_color_to_u32(lv_obj_get_style_image_recolor(icon_content, LV_PART_MAIN)) & 0xFFFFFFu, 0x395059u);
    EXPECT_EQ(lv_obj_get_style_image_recolor_opa(icon_content, LV_PART_MAIN), LV_OPA_COVER);
    option_selector_widget_impl::activate(state(), 1);
    EXPECT_EQ(lv_color_to_u32(lv_obj_get_style_image_recolor(icon_content, LV_PART_MAIN)) & 0xFFFFFFu, 0xFFFFFFu);
    option_selector_widget_impl::activate(state(), 0);
    EXPECT_EQ(lv_color_to_u32(lv_obj_get_style_image_recolor(icon_content, LV_PART_MAIN)) & 0xFFFFFFu, 0x395059u);
    for (const auto& size : {lv_point_t{240, 100}, lv_point_t{160, 140}}) {
        lv_obj_set_size(button, size.x, size.y);
        lv_obj_update_layout(button);
        option_selector_widget_impl::tick(button, &config, &runtime);
        lv_obj_update_layout(button);
        const auto& text = state()->slots[0];
        const auto& icon = state()->slots[1];
        option_selector_widget_impl::activate(state(), 1);
        EXPECT_EQ(lv_obj_get_style_bg_opa(icon.badge, LV_PART_MAIN), LV_OPA_COVER);
        EXPECT_EQ(lv_obj_get_width(icon.badge), lv_obj_get_width(text.badge));
        EXPECT_EQ(lv_obj_get_height(icon.badge), lv_obj_get_height(text.badge));
        EXPECT_EQ(lv_obj_get_width(icon.hit), size.x / 2);
        EXPECT_EQ(lv_obj_get_height(icon.hit), size.y);
        const int square_size = std::min(size.x / 2 - 8, size.y - 8);
        EXPECT_EQ(lv_image_get_scale_x(icon.content), (square_size - 8) * LV_SCALE_NONE / 20);
        lv_area_t badge_area{}, icon_area{};
        lv_obj_get_coords(icon.badge, &badge_area);
        lv_obj_get_coords(icon.content, &icon_area);
        EXPECT_NEAR(badge_area.x1 + badge_area.x2, icon_area.x1 + icon_area.x2, 1);
        EXPECT_NEAR(badge_area.y1 + badge_area.y2, icon_area.y1 + icon_area.y2, 1);
    }
}

TEST_F(OptionSelectorNativeWidget, IconInheritsWhiteOnInitialRender) {
    g_selector_icon = selector_test_icon();
    g_selector_config.slots[1] = {};
    g_selector_config.slots[1].kind = OptionSelectorContent::Icon;
    g_selector_config.slots[1].content.icon.mono = true;
    lv_obj_set_style_text_color(button, lv_color_white(), 0);
    create(OptionSelectorMode::None);
    lv_obj_t* icon = state()->slots[1].content;
    EXPECT_EQ(lv_color_to_u32(lv_obj_get_style_image_recolor(icon, LV_PART_MAIN)) & 0xFFFFFFu, 0xFFFFFFu);
}

TEST_F(OptionSelectorNativeWidget, MissingIconUsesConfiguredForeground) {
    g_selector_config.slots[1] = {};
    g_selector_config.slots[1].kind = OptionSelectorContent::Icon;
    strlcpy(g_selector_config.slots[1].foreground, "#123456",
            sizeof(g_selector_config.slots[1].foreground));
    create(OptionSelectorMode::None);
    const auto& icon = state()->slots[1];
    ASSERT_TRUE(lv_obj_check_type(icon.content, &lv_label_class));
    EXPECT_STREQ(lv_label_get_text(icon.content), "?");
    EXPECT_EQ(lv_color_to_u32(lv_obj_get_style_text_color(icon.content, LV_PART_MAIN)) & 0xFFFFFFu, 0x123456u);
}

TEST_F(OptionSelectorNativeWidget, SelectedPillsStayInsideRoundedGroup) {
    static uint8_t draw_buffer[320 * 240 * 3];
    std::vector<uint8_t> pixels(sizeof(draw_buffer));
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB888);
    lv_display_set_buffers(display, draw_buffer, nullptr, sizeof(draw_buffer), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_user_data(display, &pixels);
    lv_display_set_flush_cb(display, [](lv_display_t* output, const lv_area_t*, uint8_t* data) {
        auto& image = *static_cast<std::vector<uint8_t>*>(lv_display_get_user_data(output));
        std::copy(data, data + image.size(), image.begin());
        lv_display_flush_ready(output);
    });
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);
    lv_obj_set_pos(button, 10, 10);
    lv_obj_set_size(button, 260, 180);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    g_selector_config.group_background = true;
    g_selector_config.option_inset = 4;
    strlcpy(g_selector_config.group_color, "#808080", sizeof(g_selector_config.group_color));
    strlcpy(g_selector_config.selected_background, "#0080FF", sizeof(g_selector_config.selected_background));
    g_selector_config.slots[2].kind = OptionSelectorContent::Label;
    strlcpy(g_selector_config.slots[2].value, "auto", sizeof(g_selector_config.slots[2].value));
    strlcpy(g_selector_config.slots[2].content.label, "auto", sizeof(g_selector_config.slots[2].content.label));
    create(OptionSelectorMode::Local);
    for (int width : {260, 180}) {
        g_selector_config.option_inset = width == 260 ? 4 : 0;
        lv_obj_set_size(button, width, 180);
        lv_obj_update_layout(button);
        option_selector_widget_impl::tick(button, &config, &runtime);
        for (unsigned index = 0; index < 3; ++index)
            lv_obj_add_flag(state()->slots[index].hit, LV_OBJ_FLAG_HIDDEN);
        lv_refr_now(display);
        const auto group_pixels = pixels;
        for (unsigned index = 0; index < 3; ++index)
            lv_obj_clear_flag(state()->slots[index].hit, LV_OBJ_FLAG_HIDDEN);
        for (unsigned selected : {0u, 2u}) {
            option_selector_widget_impl::activate(state(), selected);
            lv_refr_now(display);
            unsigned blue_pixels = 0;
            unsigned outside_pixels = 0;
            for (size_t offset = 0; offset < pixels.size(); offset += 3) {
                if (pixels[offset] != 255 || pixels[offset + 1] != 128 || pixels[offset + 2] != 0) continue;
                ++blue_pixels;
                if (group_pixels[offset] == 0 && group_pixels[offset + 1] == 0 && group_pixels[offset + 2] == 255)
                    ++outside_pixels;
            }
            EXPECT_GT(blue_pixels, 100u);
            EXPECT_EQ(outside_pixels, 0u);
        }
    }
    lv_display_set_user_data(display, nullptr);
    lv_display_set_flush_cb(display, [](lv_display_t* output, const lv_area_t*, uint8_t*) {
        lv_display_flush_ready(output);
    });
}

TEST_F(OptionSelectorNativeWidget, SharedBackgroundFollowsBindingAndSelectionPrecedence) {
    g_selector_config.option_backgrounds = true;
    strlcpy(g_selector_config.option_background, "[pad:background]", sizeof(g_selector_config.option_background));
    strlcpy(g_selector_config.selected_background, "#ABCDEF", sizeof(g_selector_config.selected_background));
    strlcpy(g_selector_config.slots[1].background, "#445566", sizeof(g_selector_config.slots[1].background));
    g_selector_background = "#123456";
    create(OptionSelectorMode::Local);
    auto background = [&](unsigned index) {
        return lv_color_to_u32(lv_obj_get_style_bg_color(state()->slots[index].badge, LV_PART_MAIN)) & 0xFFFFFFu;
    };
    EXPECT_EQ(background(0), 0x123456u);
    EXPECT_EQ(background(1), 0x445566u);
    g_selector_background = "#654321";
    option_selector_widget_impl::tick(button, &config, &runtime);
    EXPECT_EQ(background(0), 0x654321u);
    option_selector_widget_impl::activate(state(), 0);
    EXPECT_EQ(background(0), 0xABCDEFu);
    option_selector_widget_impl::activate(state(), 1);
    EXPECT_EQ(background(0), 0x654321u);
    EXPECT_EQ(background(1), 0x445566u);
    g_selector_background = "invalid";
    option_selector_widget_impl::tick(button, &config, &runtime);
    EXPECT_EQ(background(0), widget_default_rgb(DEFAULT_WIDGET_TRACK_COLOR));
}

TEST_F(OptionSelectorNativeWidget, NoneDispatchesRepeatedValuesAndLocalResetsOnHide) {
    create(OptionSelectorMode::None);
    option_selector_widget_impl::activate(state(), 0);
    option_selector_widget_impl::activate(state(), 0);
    EXPECT_EQ(g_dispatches, 2u);
    EXPECT_STREQ(g_dispatched_value, "cool");

    option_selector_widget_impl::destroy(&runtime);
    lv_obj_clean(button);
    runtime = WidgetState{};
    runtime.font_owner = &fonts;
    create(OptionSelectorMode::Local);
    option_selector_widget_impl::activate(state(), 1);
    EXPECT_STREQ(g_dispatched_value, "heat");
    EXPECT_EQ(state()->selected, 1);
    option_selector_widget_impl::hide(&runtime);
    EXPECT_EQ(state()->selected, option_selector_widget_impl::NO_SLOT);
}

TEST_F(OptionSelectorNativeWidget, EmptyLocalValueSelectsAndExpandsToEmpty) {
    g_selector_config.slots[0].value[0] = '\0';
    strlcpy(source.actions[0].payload.mqtt.mqtt_payload, "before{value}after",
            sizeof(source.actions[0].payload.mqtt.mqtt_payload));
    create(OptionSelectorMode::Local);
    option_selector_widget_impl::activate(state(), 0);
    EXPECT_EQ(state()->selected, 0);
    EXPECT_EQ(g_dispatches, 1u);
    EXPECT_STREQ(g_dispatched_value, "beforeafter");
}

TEST_F(OptionSelectorNativeWidget, ValidatesActionsForEmptyAndNonemptyOptionValues) {
    JsonDocument doc;
    doc["widget_option_label"] = "Auto";
    doc["widget_option_label_2"] = "Manual";
    doc["widget_option_value_2"] = "manual";
    JsonArray actions = doc.createNestedArray("actions");
    actions.createNestedObject()["type"] = "mqtt";
    g_validated_option_values.clear();
    ASSERT_EQ(option_selector_widget_impl::validate_actions(doc.as<JsonObjectConst>(), actions), nullptr);
    EXPECT_EQ(g_validated_option_values, (std::vector<std::string>{"", "manual"}));
}

TEST_F(OptionSelectorNativeWidget, BoundWaitsForConfirmationOrTimeoutAndBlocksUnavailableInput) {
    create(OptionSelectorMode::Bound);
    option_selector_widget_impl::tick(button, &config, &runtime);
    option_selector_widget_impl::activate(state(), 1);
    EXPECT_EQ(g_dispatches, 1u);
    EXPECT_EQ(lv_obj_get_style_opa(state()->slots[0].hit, LV_PART_MAIN), LV_OPA_50);
    option_selector_widget_impl::activate(state(), 1);
    EXPECT_EQ(g_dispatches, 1u);
    g_binding_value = "heat";
    option_selector_widget_impl::tick(button, &config, &runtime);
    EXPECT_EQ(lv_obj_get_style_opa(state()->slots[0].hit, LV_PART_MAIN), LV_OPA_COVER);
    option_selector_widget_impl::activate(state(), 0);
    EXPECT_EQ(g_dispatches, 2u);
    lv_tick_inc(5000);
    option_selector_widget_impl::tick(button, &config, &runtime);
    option_selector_widget_impl::activate(state(), 0);
    EXPECT_EQ(g_dispatches, 3u);
    g_selector_source_available = false;
    option_selector_widget_impl::tick(button, &config, &runtime);
    option_selector_widget_impl::activate(state(), 0);
    EXPECT_EQ(g_dispatches, 3u);
}

TEST_F(OptionSelectorNativeWidget, FailedActionRetainsLocalSelectionButClearsBoundPending) {
    g_dispatch_result = ACTION_FAILED;
    create(OptionSelectorMode::Local);
    option_selector_widget_impl::activate(state(), 1);
    EXPECT_EQ(state()->selected, 1);

    option_selector_widget_impl::destroy(&runtime);
    lv_obj_clean(button);
    runtime = WidgetState{};
    runtime.font_owner = &fonts;
    create(OptionSelectorMode::Bound);
    option_selector_widget_impl::tick(button, &config, &runtime);
    option_selector_widget_impl::activate(state(), 1);
    EXPECT_EQ(state()->pending, option_selector_widget_impl::NO_SLOT);
    option_selector_widget_impl::activate(state(), 1);
    EXPECT_EQ(g_dispatches, 3u);
}