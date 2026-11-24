#include <gtest/gtest.h>
#include "native_extension_ui.h"

namespace {

TEST(ExtensionAbi, RejectsOtherMajorsAndFutureMinors) {
    EXPECT_TRUE(native_extension_abi_compatible(NATIVE_EXTENSION_ABI_VERSION, 0));
    EXPECT_FALSE(native_extension_abi_compatible(NATIVE_EXTENSION_ABI_VERSION - 1, 0));
    EXPECT_FALSE(native_extension_abi_compatible(NATIVE_EXTENSION_ABI_VERSION,
                                                  NATIVE_EXTENSION_ABI_MINOR + 1));
}

struct Received {
    int calls;
    void* target;
    NativeExtensionLvglEventCode code;
};

void receive(void* context, void* target, NativeExtensionLvglEventCode code) {
    auto* received = static_cast<Received*>(context);
    ++received->calls;
    received->target = target;
    received->code = code;
}

class ExtensionEvents : public testing::Test {
protected:
    void SetUp() override {
        lv_init();
        display = lv_display_create(320, 240);
        object = lv_obj_create(lv_screen_active());
    }
    void TearDown() override {
        lv_display_delete(display);
        lv_deinit();
    }
    lv_display_t* display = nullptr;
    lv_obj_t* object = nullptr;
};

TEST_F(ExtensionEvents, MapsExtensionAlignmentsToLvgl) {
    const struct {
        NativeExtensionAlign extension;
        lv_align_t lvgl;
    } alignments[] = {
        {NATIVE_EXTENSION_ALIGN_TOP_LEFT, LV_ALIGN_TOP_LEFT},
        {NATIVE_EXTENSION_ALIGN_TOP_MID, LV_ALIGN_TOP_MID},
        {NATIVE_EXTENSION_ALIGN_TOP_RIGHT, LV_ALIGN_TOP_RIGHT},
        {NATIVE_EXTENSION_ALIGN_LEFT_MID, LV_ALIGN_LEFT_MID},
        {NATIVE_EXTENSION_ALIGN_CENTER, LV_ALIGN_CENTER},
        {NATIVE_EXTENSION_ALIGN_RIGHT_MID, LV_ALIGN_RIGHT_MID},
        {NATIVE_EXTENSION_ALIGN_BOTTOM_LEFT, LV_ALIGN_BOTTOM_LEFT},
        {NATIVE_EXTENSION_ALIGN_BOTTOM_MID, LV_ALIGN_BOTTOM_MID},
        {NATIVE_EXTENSION_ALIGN_BOTTOM_RIGHT, LV_ALIGN_BOTTOM_RIGHT},
    };
    for (const auto& alignment : alignments) {
        ASSERT_TRUE(native_extension_ui_align(object, alignment.extension, 0, 0));
        EXPECT_EQ(lv_obj_get_style_align(object, LV_PART_MAIN), alignment.lvgl);
    }
    EXPECT_FALSE(native_extension_ui_align(object, static_cast<NativeExtensionAlign>(255), 0, 0));
}

TEST_F(ExtensionEvents, RoutesContextAndSupportsCancellation) {
    Received first{};
    Received second{};
    NativeExtensionUiEventBinding a{receive, &first};
    NativeExtensionUiEventBinding b{receive, &second};
    void* handler = native_extension_ui_event_add(object, NATIVE_EXTENSION_EVENT_CLICKED, &a);
    ASSERT_NE(handler, nullptr);
    ASSERT_NE(native_extension_ui_event_add(object, NATIVE_EXTENSION_EVENT_PRESS_LOST, &b), nullptr);
    lv_obj_send_event(object, LV_EVENT_CLICKED, nullptr);
    EXPECT_EQ(first.calls, 1);
    EXPECT_EQ(first.target, object);
    EXPECT_EQ(first.code, NATIVE_EXTENSION_EVENT_CLICKED);
    EXPECT_EQ(second.calls, 0);
    ASSERT_TRUE(native_extension_ui_event_remove(object, handler));
    lv_obj_send_event(object, LV_EVENT_CLICKED, nullptr);
    lv_obj_send_event(object, LV_EVENT_PRESS_LOST, nullptr);
    EXPECT_EQ(first.calls, 1);
    EXPECT_EQ(second.calls, 1);
    EXPECT_EQ(second.code, NATIVE_EXTENSION_EVENT_PRESS_LOST);
}

TEST_F(ExtensionEvents, DeleteAndInvalidRegistration) {
    Received received{};
    NativeExtensionUiEventBinding binding{receive, &received};
    EXPECT_EQ(native_extension_ui_event_add(nullptr, NATIVE_EXTENSION_EVENT_CLICKED, &binding), nullptr);
    EXPECT_EQ(native_extension_ui_event_add(object, static_cast<NativeExtensionLvglEventCode>(255), &binding), nullptr);
    ASSERT_NE(native_extension_ui_event_add(object, NATIVE_EXTENSION_EVENT_DELETE, &binding), nullptr);
    lv_obj_delete(object);
    object = nullptr;
    EXPECT_EQ(received.calls, 1);
    EXPECT_EQ(received.code, NATIVE_EXTENSION_EVENT_DELETE);
}

} // namespace
