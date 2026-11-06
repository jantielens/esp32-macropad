#include <gtest/gtest.h>
#include <lvgl.h>

#include "widgets/external_widget.cpp"

static bool stopping = false;
static bool stop_on_destroy = false;
static unsigned creates = 0;
static unsigned destroys = 0;
static unsigned ticks = 0;
static int created_width = 0;
static int created_height = 0;

extern "C" unsigned long millis() { return 10000; }
extern "C" unsigned long micros() { return 10000000; }
void widget_register(const WidgetType*) {}
bool PadScreen::isOverlayButton(const ButtonTile*) const { return false; }
bool screen_saver_manager_is_rendering_suspended() { return false; }
void device_telemetry_mark_lvgl_extension_tick(const char*) {}
void device_telemetry_mark_lvgl_extension_tick_complete(uint32_t) {}
void native_extension_set_instance_binding_context(const char*, uint32_t, const PadBinding*, uint8_t) {}
void native_extension_set_instance_button_context(const char*, uint32_t, void*, void*, void*, void*) {}
void native_extension_clear_instance_binding_context(const char*, uint32_t) {}
bool native_extension_is_stopping(const char*) { return stopping; }
uint16_t native_extension_tick_interval_ms(const char*) { return 500; }
void native_extension_tick_instance(const char*, uint32_t) { ++ticks; }
void native_extension_destroy_instance(const char*, uint32_t) {
    ++destroys;
    stopping = stop_on_destroy;
}
bool native_extension_create_instance(const char*, uint32_t, void* root, const char*, bool* failed) {
    *failed = false;
    if (stopping) return false;
    ++creates;
    auto* object = static_cast<lv_obj_t*>(root);
    created_width = lv_obj_get_width(object);
    created_height = lv_obj_get_height(object);
    lv_obj_create(object);
    return true;
}

class ExternalWidget : public testing::Test {
protected:
    lv_display_t* display = nullptr;
    lv_obj_t* parent = nullptr;
    ButtonTile button{};
    ScreenButtonConfig source{};
    PadRect rect{};
    UIScaleInfo scale{};

    ExternalWidgetState* state() {
        return reinterpret_cast<ExternalWidgetState*>(button.widget_state.data);
    }

    void SetUp() override {
        static bool initialized = false;
        if (!initialized) { lv_init(); initialized = true; }
        stopping = stop_on_destroy = false;
        creates = destroys = ticks = 0;
        display = lv_display_create(320, 240);
        parent = lv_obj_create(lv_display_get_screen_active(display));
        lv_obj_remove_style_all(parent);
        lv_obj_set_size(parent, 200, 120);
        auto* config = reinterpret_cast<ExternalWidgetConfig*>(button.widget_cfg.data);
        strlcpy(config->extension_id, "test", sizeof(config->extension_id));
        config->upscale = 2;
        external_create(parent, &button.widget_cfg, &source, &rect, &scale, &button.widget_state);
    }

    void TearDown() override {
        external_destroy(&button.widget_state);
        lv_display_delete(display);
    }

    void resize(int width, int height) {
        lv_obj_set_size(parent, width, height);
        lv_obj_update_layout(parent);
        external_tick(parent, &button.widget_cfg, &button.widget_state);
    }
};

TEST_F(ExternalWidget, ResizingRecreatesOnceAndReplacesOldObjects) {
    ASSERT_TRUE(state()->created);
    ASSERT_EQ(creates, 1u);
    EXPECT_EQ(created_width, 100);
    EXPECT_EQ(created_height, 60);
    resize(120, 80);
    EXPECT_EQ(destroys, 1u);
    EXPECT_EQ(creates, 2u);
    EXPECT_EQ(created_width, 60);
    EXPECT_EQ(created_height, 40);
    EXPECT_EQ(lv_obj_get_child_count(state()->root), 1u);
    resize(120, 80);
    EXPECT_EQ(destroys, 1u);
    EXPECT_EQ(creates, 2u);
}

TEST_F(ExternalWidget, PhysicalResizeRecreatesEvenWhenUpscaledSizeRoundsTheSame) {
    resize(199, 119);
    EXPECT_EQ(destroys, 1u);
    EXPECT_EQ(creates, 2u);
    EXPECT_EQ(created_width, 100);
    EXPECT_EQ(created_height, 60);
}

TEST_F(ExternalWidget, ResizingWaitsForStoppingWorkerBeforeRecreating) {
    stop_on_destroy = true;
    resize(150, 100);
    EXPECT_EQ(destroys, 1u);
    EXPECT_EQ(creates, 1u);
    ASSERT_FALSE(state()->created);
    ASSERT_TRUE(state()->retry_after_stop);
    ASSERT_NE(state()->status_label, nullptr);
    resize(140, 90);
    EXPECT_EQ(destroys, 1u);
    stopping = false;
    external_timer_cb(state()->timer);
    EXPECT_TRUE(state()->created);
    EXPECT_EQ(creates, 2u);
    EXPECT_EQ(created_width, 70);
    EXPECT_EQ(created_height, 45);
    EXPECT_EQ(state()->status_label, nullptr);
}
