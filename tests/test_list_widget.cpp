#include <gtest/gtest.h>
#include <vector>
#include "widgets/list_widget.cpp"
#include "item_binding_scheme.h"

void widget_register(const WidgetType*) {}
void display_manager_lock_if_needed(bool* locked) { *locked = false; }
void display_manager_unlock_if_needed(bool) {}
bool ota_activity_is_active() { return false; }
extern "C" unsigned long millis() { return 0; }
extern "C" void mqtt_sub_store_ensure_binding_subscribed(const char*) {}

static std::string activated_id;
static unsigned activations = 0;
static lv_event_code_t last_feedback = LV_EVENT_ALL;
static lv_event_code_t activation_feedback = LV_EVENT_ALL;
static WidgetState* destroy_on_dispatch = nullptr;
extern ButtonDefaults list_test_defaults;
ActionResult action_list_dispatch_result(const ButtonAction*, uint8_t count, const char*,
                                         ActionContinuationOwner, bool) {
    if (count) {
        activation_feedback = last_feedback;
        char id[64];
        EXPECT_EQ(item_binding_resolve("id", id, sizeof(id)), BINDING_RESOLVER_RESOLVED);
        activated_id = id;
        ++activations;
    }
    if (destroy_on_dispatch) {
        WidgetState* runtime = destroy_on_dispatch;
        destroy_on_dispatch = nullptr;
        list_widget_impl::destroy(runtime);
    }
    return ACTION_COMPLETE;
}

namespace {
uint32_t revision = 1;
bool omit_second = false;
const char* first_title = "first";
uint32_t source_revision() { return revision; }
ListSourceLoadStep load(void**, ListSourceSnapshot* snapshot, const char** error) {
    JsonDocument item;
    for (const char* id : {"first", "second", "third"}) {
        if (omit_second && !strcmp(id, "second")) continue;
        item["id"] = id;
        if (!strcmp(id, "third")) item.remove("title");
        else item["title"] = !strcmp(id, "first") ? first_title : id;
        if ((*error = snapshot->append(item.as<JsonObjectConst>())))
            return ListSourceLoadStep::Failed;
    }
    return ListSourceLoadStep::Complete;
}
void finish(void*) {}
const ListSourceField fields[] = {
    {"id", "ID", ListSourceFieldType::String, false},
    {"title", "Title", ListSourceFieldType::String, false},
};
const ListSourceDescriptor source{"[test:items]", "Items", fields, 2, source_revision, load, finish};
}

class ListWidget : public testing::Test {
protected:
    lv_display_t* display = nullptr;
    lv_indev_t* input = nullptr;
    lv_obj_t* button = nullptr;
    lv_point_t point{};
    bool pressed = false;
    ButtonTile tile{};
    WidgetConfig config{};
    std::vector<WidgetPressFeedback> feedback_events;

    void SetUp() override {
        static bool initialized = false;
        if (!initialized) {
            lv_init();
            item_binding_init();
            pad_binding_init();
            ASSERT_TRUE(list_source_register(&source));
            initialized = true;
        }
        activated_id.clear();
        activations = 0;
        last_feedback = activation_feedback = LV_EVENT_ALL;
        destroy_on_dispatch = nullptr;
        omit_second = false;
        first_title = "first";
        list_test_defaults = {};
        display = lv_display_create(320, 240);
        button = lv_obj_create(lv_screen_active());
        tile.obj = button;
        lv_obj_add_event_cb(button, [](lv_event_t* event) {
            auto* self = static_cast<ListWidget*>(lv_event_get_user_data(event));
            const auto* feedback = static_cast<const WidgetPressFeedback*>(lv_event_get_param(event));
            self->feedback_events.push_back(*feedback);
            last_feedback = feedback->event;
        }, LV_EVENT_VALUE_CHANGED, this);
        lv_obj_set_size(button, 200, 125);
        lv_obj_set_style_pad_all(button, 0, 0);
        lv_obj_set_style_border_width(button, 0, 0);
        input = lv_indev_create();
        lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
        lv_indev_set_user_data(input, this);
        lv_indev_set_read_cb(input, [](lv_indev_t* device, lv_indev_data_t* data) {
            auto* self = static_cast<ListWidget*>(lv_indev_get_user_data(device));
            data->point = self->point;
            data->state = self->pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        });
        JsonDocument json;
        json["widget_data_binding"] = source.token;
        json["widget_list_id"] = "notes";
        json["widget_list_viewport"] = "paged";
        json["widget_list_visible_items"] = 2;
        json["widget_list_bg_color"] = "#ffffff";
        json["widget_list_border_width"] = 1;
        json["widget_list_border_color"] = "#000000";
        json["widget_list_selected_bg_color"] = "#000000";
        json["widget_list_selected_border_width"] = 3;
        json["widget_list_selected_border_color"] = "#ffffff";
        json["widget_list_selected_top_color"] = "#ffffff";
        ASSERT_TRUE(list_parse_config(json.as<JsonObject>(), config.data));
        tile.action_count = 1;
        tile.lp_action_count = 1;
        list_widget_impl::create(button, &config, nullptr, nullptr, &pad_get_scale_info(), &tile.widget_state);
        list_widget_impl::show(&tile.widget_state);
        list_source_loop();
        list_widget_impl::tick(button, &config, &tile.widget_state);
    }

    void TearDown() override {
        list_widget_impl::destroy(&tile.widget_state);
        list_release_config(config.data);
        list_source_loop();
        lv_indev_delete(input);
        lv_display_delete(display);
    }

    lv_obj_t* row(unsigned index) {
        return lv_obj_get_child(lv_obj_get_child(button, 0), index);
    }
    void press(unsigned index) {
        lv_obj_update_layout(button);
        lv_area_t bounds;
        lv_obj_get_coords(row(index), &bounds);
        point = {(bounds.x1 + bounds.x2) / 2, (bounds.y1 + bounds.y2) / 2};
        pressed = true;
        lv_indev_read(input);
    }
    void tap(unsigned index, bool hold = false) {
        press(index);
        if (hold) lv_obj_send_event(row(index), LV_EVENT_LONG_PRESSED, input);
        pressed = false;
        lv_indev_read(input);
    }
    void use_scroll_viewport(const char* track = "", const char* indicator = "", const char* row_background = "",
                             unsigned spacing = 4, bool allow_selection = true) {
        JsonDocument json;
        json["widget_data_binding"] = source.token;
        json["widget_list_id"] = "notes";
        json["widget_list_viewport"] = "scroll";
        json["widget_list_visible_items"] = 2;
        json["widget_list_spacing"] = spacing;
        json["widget_list_allow_selection"] = allow_selection;
        json["widget_list_indicator_track_color"] = track;
        json["widget_list_indicator_color"] = indicator;
        json["widget_list_bg_color"] = row_background;
        reconfigure(json.as<JsonObject>());
    }
    void reconfigure(JsonObject json) {
        list_widget_impl::destroy(&tile.widget_state);
        list_release_config(config.data);
        list_source_loop();
        json["widget_data_binding"] = source.token;
        json["widget_list_id"] = "notes";
        ASSERT_TRUE(list_parse_config(json, config.data));
        list_widget_impl::create(button, &config, nullptr, nullptr, &pad_get_scale_info(), &tile.widget_state);
        list_widget_impl::show(&tile.widget_state);
        list_source_loop();
        list_widget_impl::tick(button, &config, &tile.widget_state);
    }
    void resize_and_tick(int32_t height) {
        lv_obj_set_height(button, height);
        lv_obj_update_layout(button);
        list_widget_impl::tick(button, &config, &tile.widget_state);
        lv_obj_update_layout(button);
    }
};

TEST_F(ListWidget, TapAndHoldSendRowFeedbackBeforeDispatch) {
    lv_obj_t* first = row(0);
    tap(0);
    ASSERT_EQ(feedback_events.size(), 3u);
    EXPECT_EQ(feedback_events[0].event, LV_EVENT_PRESSED);
    EXPECT_EQ(feedback_events[0].object, first);
    EXPECT_TRUE(feedback_events[0].has_background);
    EXPECT_EQ(feedback_events[0].background, 0xffffffu);
    EXPECT_EQ(feedback_events[1].event, LV_EVENT_RELEASED);
    EXPECT_EQ(feedback_events[2].event, LV_EVENT_CLICKED);
    EXPECT_EQ(activation_feedback, LV_EVENT_CLICKED);

    feedback_events.clear();
    tap(1, true);
    ASSERT_EQ(feedback_events.size(), 3u);
    EXPECT_EQ(feedback_events[0].event, LV_EVENT_PRESSED);
    EXPECT_EQ(feedback_events[0].object, row(1));
    EXPECT_EQ(feedback_events[1].event, LV_EVENT_LONG_PRESSED);
    EXPECT_EQ(feedback_events[2].event, LV_EVENT_RELEASED);
    EXPECT_EQ(activation_feedback, LV_EVENT_LONG_PRESSED);
    EXPECT_EQ(activations, 2u);
}

TEST_F(ListWidget, CancelledPressClearsFeedbackWithoutActivating) {
    press(0);
    lv_obj_send_event(row(0), LV_EVENT_PRESS_LOST, input);
    pressed = false;
    lv_indev_read(input);
    ASSERT_EQ(feedback_events.size(), 2u);
    EXPECT_EQ(feedback_events.back().event, LV_EVENT_PRESS_LOST);
    EXPECT_EQ(activations, 0u);

    feedback_events.clear();
    press(0);
    first_title = "updated";
    ++revision;
    list_source_loop();
    list_widget_impl::tick(button, &config, &tile.widget_state);
    pressed = false;
    lv_indev_read(input);
    ASSERT_EQ(feedback_events.size(), 2u);
    EXPECT_EQ(feedback_events.back().event, LV_EVENT_PRESS_LOST);
    EXPECT_EQ(activations, 0u);

    feedback_events.clear();
    press(0);
    list_widget_impl::hide(&tile.widget_state);
    pressed = false;
    lv_indev_read(input);
    ASSERT_EQ(feedback_events.size(), 2u);
    EXPECT_EQ(feedback_events.back().event, LV_EVENT_PRESS_LOST);
    EXPECT_EQ(activations, 0u);
}

TEST_F(ListWidget, ScrollingCancelsFeedbackAndPagingDoesNotActivate) {
    use_scroll_viewport();
    press(0);
    point.y -= 20;
    lv_indev_read(input);
    pressed = false;
    lv_indev_read(input);
    ASSERT_EQ(feedback_events.size(), 2u);
    EXPECT_EQ(feedback_events.back().event, LV_EVENT_PRESS_LOST);
    EXPECT_EQ(activations, 0u);
    feedback_events.clear();
    EXPECT_TRUE(list_widget_command(&tile.widget_state, "next_page"));
    EXPECT_TRUE(feedback_events.empty());
}

TEST_F(ListWidget, SelectionWithoutActionsStillSendsActivationFeedback) {
    tile.action_count = 0;
    tap(0);
    ASSERT_EQ(feedback_events.size(), 3u);
    EXPECT_EQ(feedback_events[0].event, LV_EVENT_PRESSED);
    EXPECT_EQ(feedback_events[1].event, LV_EVENT_RELEASED);
    EXPECT_EQ(feedback_events[2].event, LV_EVENT_CLICKED);
    EXPECT_EQ(activations, 0u);

    feedback_events.clear();
    tap(0, true);
    EXPECT_EQ(activation_feedback, LV_EVENT_LONG_PRESSED);
    EXPECT_EQ(activations, 1u);

    tile.lp_action_count = 0;
    feedback_events.clear();
    tap(0);
    ASSERT_EQ(feedback_events.size(), 3u);
    EXPECT_EQ(feedback_events.back().event, LV_EVENT_CLICKED);
    EXPECT_EQ(activations, 1u);
}

TEST_F(ListWidget, DisabledSelectionPreservesActionsButNotSelectedAppearance) {
    use_scroll_viewport("", "", "", 4, false);
    auto* state = list_widget_impl::get(&tile.widget_state);
    const auto border = lv_obj_get_style_border_width(row(0), LV_PART_MAIN);
    const auto background = lv_color_to_u32(lv_obj_get_style_bg_color(row(0), LV_PART_MAIN));
    tap(0);
    EXPECT_EQ(activated_id, "first");
    EXPECT_EQ(activation_feedback, LV_EVENT_CLICKED);
    EXPECT_EQ(state->focus, list_widget_impl::none);
    EXPECT_EQ(lv_obj_get_style_border_width(row(0), LV_PART_MAIN), border);
    EXPECT_EQ(lv_color_to_u32(lv_obj_get_style_bg_color(row(0), LV_PART_MAIN)), background);
    tap(1, true);
    EXPECT_EQ(activated_id, "second");
    EXPECT_EQ(activation_feedback, LV_EVENT_LONG_PRESSED);
    EXPECT_EQ(activations, 2u);
    EXPECT_EQ(state->focus, list_widget_impl::none);

    tile.action_count = tile.lp_action_count = 0;
    feedback_events.clear();
    tap(0);
    EXPECT_TRUE(feedback_events.empty());
    EXPECT_EQ(state->focus, list_widget_impl::none);
    EXPECT_EQ(activations, 2u);
}

TEST_F(ListWidget, HoldActionCanDestroyListWithActiveFeedback) {
    destroy_on_dispatch = &tile.widget_state;
    tap(0, true);
    EXPECT_EQ(activated_id, "first");
    EXPECT_EQ(activations, 1u);
    EXPECT_EQ(activation_feedback, LV_EVENT_LONG_PRESSED);
    ASSERT_EQ(feedback_events.size(), 3u);
    EXPECT_EQ(feedback_events.back().event, LV_EVENT_PRESS_LOST);
}

TEST_F(ListWidget, SelectionAndPagingAreIndependent) {
    ASSERT_NE(row(0), nullptr);
    EXPECT_STREQ(lv_label_get_text(lv_obj_get_child(row(0), 0)), "first");
    EXPECT_STREQ(lv_label_get_text(lv_obj_get_child(row(2), 0)), "Untitled");
    EXPECT_EQ(lv_obj_get_style_border_width(row(0), LV_PART_MAIN), 1);
    tap(0);
    EXPECT_EQ(activated_id, "first");
    EXPECT_EQ(lv_obj_get_style_border_width(row(0), LV_PART_MAIN), 3);
    EXPECT_EQ(lv_color_to_u32(lv_obj_get_style_bg_color(row(0), LV_PART_MAIN)) & 0xffffff, 0u);
    EXPECT_EQ(lv_color_to_u32(lv_obj_get_style_text_color(lv_obj_get_child(row(0), 0), LV_PART_MAIN)) & 0xffffff, 0xffffffu);
    EXPECT_TRUE(list_widget_command(&tile.widget_state, "next_page"));
    EXPECT_FALSE(lv_obj_has_flag(row(2), LV_OBJ_FLAG_HIDDEN));
    EXPECT_EQ(activations, 1u);
    EXPECT_TRUE(list_widget_command(&tile.widget_state, "previous_page"));
    EXPECT_EQ(lv_obj_get_style_border_width(row(0), LV_PART_MAIN), 3);
    EXPECT_TRUE(list_widget_command(&tile.widget_state, "next_page"));
    tap(2, true);
    EXPECT_EQ(activated_id, "third");
    EXPECT_EQ(activations, 2u);
    EXPECT_FALSE(list_widget_command(&tile.widget_state, "activate"));
}

TEST_F(ListWidget, HideResetsSelection) {
    tap(0);
    list_widget_impl::hide(&tile.widget_state);
    list_widget_impl::show(&tile.widget_state);
    list_source_loop();
    list_widget_impl::tick(button, &config, &tile.widget_state);
    EXPECT_EQ(lv_obj_get_style_border_width(row(0), LV_PART_MAIN), 1);
    EXPECT_EQ(activations, 1u);
}

TEST_F(ListWidget, RefreshPreservesSelectedItem) {
    tap(1);
    ++revision;
    list_source_loop();
    list_widget_impl::tick(button, &config, &tile.widget_state);
    EXPECT_EQ(lv_obj_get_style_border_width(row(1), LV_PART_MAIN), 3);
    omit_second = true;
    ++revision;
    list_source_loop();
    list_widget_impl::tick(button, &config, &tile.widget_state);
    EXPECT_EQ(lv_obj_get_style_border_width(row(0), LV_PART_MAIN), 1);
    EXPECT_EQ(lv_obj_get_style_border_width(row(1), LV_PART_MAIN), 1);
}

TEST_F(ListWidget, StableIdentitiesReuseRowsAndUnchangedSnapshotsPreservePress) {
    auto* state = list_widget_impl::get(&tile.widget_state);
    lv_obj_t* first = row(0);
    auto* rows = state->rows;
    press(0);
    ++revision;
    list_source_loop();
    list_widget_impl::tick(button, &config, &tile.widget_state);
    EXPECT_EQ(row(0), first);
    EXPECT_EQ(state->rows, rows);
    EXPECT_EQ(state->pressed, 0);
    pressed = false;
    lv_indev_read(input);
    EXPECT_EQ(activations, 1u);

    first_title = "Updated CPU";
    ++revision;
    list_source_loop();
    list_widget_impl::tick(button, &config, &tile.widget_state);
    EXPECT_EQ(row(0), first);
    EXPECT_EQ(state->rows, rows);
    EXPECT_STREQ(lv_label_get_text(lv_obj_get_child(first, 0)), "Updated CPU");
}

TEST_F(ListWidget, TinyRowsAreClippedWithoutDisablingList) {
    lv_obj_set_size(button, 15, 8);
    list_widget_impl::tick(button, &config, &tile.widget_state);
    EXPECT_GE(lv_obj_get_height(row(0)), 1);
    EXPECT_TRUE(list_widget_command(&tile.widget_state, "next_page"));
    EXPECT_FALSE(lv_obj_has_flag(row(2), LV_OBJ_FLAG_HIDDEN));
}

TEST_F(ListWidget, AutoCapacityUsesTouchMinimumAndResizesIndependentlyOfItemCount) {
    JsonDocument json;
    json["widget_list_visible_items"] = 0;
    json["widget_list_viewport"] = "paged";
    lv_obj_set_height(button, 92);
    reconfigure(json.as<JsonObject>());
    lv_obj_update_layout(button);
    auto* state = list_widget_impl::get(&tile.widget_state);
    EXPECT_EQ(state->capacity, 2);
    EXPECT_EQ(lv_obj_get_height(row(0)), 44);
    EXPECT_EQ(lv_obj_get_height(row(1)), 44);
    EXPECT_TRUE(list_widget_command(&tile.widget_state, "next_page"));
    EXPECT_FALSE(lv_obj_has_flag(row(2), LV_OBJ_FLAG_HIDDEN));

    resize_and_tick(91);
    EXPECT_EQ(state->capacity, 1);
    resize_and_tick(200);
    EXPECT_EQ(state->capacity, 4);
    EXPECT_EQ(state->count, 3);
    EXPECT_TRUE(lv_obj_has_flag(state->indicator, LV_OBJ_FLAG_HIDDEN));

    resize_and_tick(1);
    EXPECT_EQ(state->capacity, 1);
    EXPECT_EQ(lv_obj_get_height(row(0)), 1);
    resize_and_tick(2000);
    EXPECT_EQ(state->capacity, LIST_SOURCE_MAX_ITEMS);
}

TEST_F(ListWidget, AutoReservesConfiguredLabelFontsEvenWhenResolvedTextIsEmpty) {
    JsonDocument json;
    json["widget_list_viewport"] = "scroll";
    json["widget_list_style_top"] = "font_size:32";
    json["widget_list_style_bottom"] = "font_size:32";
    json["widget_list_label_bottom"] = "[item:title|]";
    const int32_t minimum_height = 2 * lv_font_get_line_height(&lv_font_montserrat_32) + 16;
    lv_obj_set_height(button, 2 * minimum_height + 4);
    reconfigure(json.as<JsonObject>());
    auto* state = list_widget_impl::get(&tile.widget_state);
    EXPECT_EQ(state->capacity, 2);
    EXPECT_EQ(lv_obj_get_height(row(0)), minimum_height);
    EXPECT_EQ(lv_obj_get_height(row(2)), minimum_height);
    EXPECT_TRUE(lv_obj_has_flag(lv_obj_get_child(row(2), 1), LV_OBJ_FLAG_HIDDEN));

    first_title = "";
    ++revision;
    list_source_loop();
    list_widget_impl::tick(button, &config, &tile.widget_state);
    EXPECT_EQ(state->capacity, 2);
    EXPECT_EQ(lv_obj_get_height(row(0)), minimum_height);
    resize_and_tick(2 * minimum_height + 3);
    EXPECT_EQ(state->capacity, 1);

    json.remove("widget_list_label_bottom");
    reconfigure(json.as<JsonObject>());
    state = list_widget_impl::get(&tile.widget_state);
    EXPECT_GT(state->capacity, 1);
}

TEST_F(ListWidget, RowsFillViewportAndUseDeviceButtonDefaults) {
    lv_obj_update_layout(button);
    auto* state = list_widget_impl::get(&tile.widget_state);
    EXPECT_EQ(lv_obj_get_height(row(0)) + lv_obj_get_height(row(1)) + state->spacing, state->height);
    strlcpy(list_test_defaults.bg_color, "#eeeeee", sizeof(list_test_defaults.bg_color));
    strlcpy(list_test_defaults.border_color, "#123456", sizeof(list_test_defaults.border_color));
    strlcpy(list_test_defaults.border_width, "3", sizeof(list_test_defaults.border_width));
    use_scroll_viewport();
    EXPECT_EQ(lv_color_to_u32(lv_obj_get_style_bg_color(row(0), LV_PART_MAIN)) & 0xffffff, 0xeeeeeeu);
    EXPECT_EQ(lv_obj_get_style_border_width(row(0), LV_PART_MAIN), 3);
    tap(0);
    EXPECT_EQ(lv_obj_get_style_border_width(row(0), LV_PART_MAIN), 6);
    EXPECT_EQ(lv_color_to_u32(lv_obj_get_style_border_color(row(0), LV_PART_MAIN)) & 0xffffff, 0x123456u);
}

TEST_F(ListWidget, ScrollPageCommandsReturnFromPartialLastPage) {
    use_scroll_viewport();
    auto* state = list_widget_impl::get(&tile.widget_state);
    tap(0);
    EXPECT_TRUE(list_widget_command(&tile.widget_state, "next_page"));
    EXPECT_GT(lv_obj_get_scroll_y(state->container), 0);
    EXPECT_EQ(activations, 1u);
    EXPECT_TRUE(list_widget_command(&tile.widget_state, "previous_page"));
    EXPECT_EQ(lv_obj_get_scroll_y(state->container), 0);
    EXPECT_EQ(state->focus, 0);
}

TEST_F(ListWidget, ScrollRowsKeepSpacingAcrossViewportGroups) {
    use_scroll_viewport("", "", "", 15);
    auto* state = list_widget_impl::get(&tile.widget_state);
    lv_obj_update_layout(button);
    lv_obj_scroll_to_y(state->container, LV_COORD_MAX, LV_ANIM_OFF);
    lv_obj_update_layout(button);
    for (unsigned i = 1; i < 3; ++i) {
        lv_area_t previous, current;
        lv_obj_get_coords(row(i - 1), &previous);
        lv_obj_get_coords(row(i), &current);
        EXPECT_EQ(current.y1 - previous.y2 - 1, 15);
    }
}

TEST_F(ListWidget, PositionIndicatorTracksPagedViewportAndOverflow) {
    auto* state = list_widget_impl::get(&tile.widget_state);
    lv_obj_update_layout(button);
    EXPECT_FALSE(lv_obj_has_flag(state->indicator, LV_OBJ_FLAG_HIDDEN));
    EXPECT_EQ(lv_obj_get_width(state->container) + 6, lv_obj_get_content_width(button));
    EXPECT_EQ(lv_bar_get_value(state->indicator), 10000);
    EXPECT_GT(lv_bar_get_start_value(state->indicator), 0);
    EXPECT_TRUE(list_widget_command(&tile.widget_state, "next_page"));
    EXPECT_EQ(lv_bar_get_start_value(state->indicator), 0);
    EXPECT_LT(lv_bar_get_value(state->indicator), 10000);
    EXPECT_TRUE(list_widget_command(&tile.widget_state, "previous_page"));
    EXPECT_EQ(lv_bar_get_value(state->indicator), 10000);

    omit_second = true;
    ++revision;
    list_source_loop();
    list_widget_impl::tick(button, &config, &tile.widget_state);
    lv_obj_update_layout(button);
    EXPECT_TRUE(lv_obj_has_flag(state->indicator, LV_OBJ_FLAG_HIDDEN));
    EXPECT_EQ(lv_obj_get_width(state->container), lv_obj_get_content_width(button));

    lv_obj_set_height(button, 1);
    lv_obj_update_layout(button);
    list_widget_impl::tick(button, &config, &tile.widget_state);
    EXPECT_FALSE(lv_obj_has_flag(state->indicator, LV_OBJ_FLAG_HIDDEN));
    lv_obj_set_size(button, 1, 1);
    lv_obj_update_layout(button);
    list_widget_impl::tick(button, &config, &tile.widget_state);
    EXPECT_TRUE(lv_obj_has_flag(state->indicator, LV_OBJ_FLAG_HIDDEN));
}

TEST_F(ListWidget, PositionIndicatorFollowsScrollAndUsesPadColorBindings) {
    PadBinding bindings[] = {{"track", "#123456"}, {"bar", "#abcdef"}};
    tile.pad_bindings = bindings;
    tile.pad_binding_count = 2;
    use_scroll_viewport("[pad:track]", "[pad:bar]");
    auto* state = list_widget_impl::get(&tile.widget_state);
    EXPECT_EQ(lv_color_to_u32(lv_obj_get_style_bg_color(state->indicator, LV_PART_MAIN)) & 0xffffff, 0x123456u);
    EXPECT_EQ(lv_color_to_u32(lv_obj_get_style_bg_color(state->indicator, LV_PART_INDICATOR)) & 0xffffff, 0xabcdefu);
    EXPECT_EQ(lv_obj_get_style_bg_opa(state->indicator, LV_PART_MAIN), LV_OPA_COVER);
    EXPECT_EQ(lv_obj_get_style_bg_opa(state->indicator, LV_PART_INDICATOR), LV_OPA_COVER);
    strlcpy(bindings[1].value, "#654321", sizeof(bindings[1].value));
    list_widget_impl::tick(button, &config, &tile.widget_state);
    EXPECT_EQ(lv_color_to_u32(lv_obj_get_style_bg_color(state->indicator, LV_PART_INDICATOR)) & 0xffffff, 0x654321u);
    EXPECT_EQ(lv_bar_get_value(state->indicator), 10000);
    lv_obj_scroll_to_y(state->container, 10, LV_ANIM_OFF);
    EXPECT_LT(lv_bar_get_value(state->indicator), 10000);
    EXPECT_TRUE(list_widget_command(&tile.widget_state, "next_page"));
    EXPECT_EQ(lv_bar_get_start_value(state->indicator), 0);
    EXPECT_TRUE(list_widget_command(&tile.widget_state, "previous_page"));
    EXPECT_EQ(lv_bar_get_value(state->indicator), 10000);
    lv_obj_set_height(button, 200);
    lv_obj_update_layout(button);
    list_widget_impl::tick(button, &config, &tile.widget_state);
    EXPECT_EQ(lv_bar_get_value(state->indicator), 10000);
    EXPECT_TRUE(list_widget_command(&tile.widget_state, "next_page"));
    EXPECT_EQ(lv_bar_get_start_value(state->indicator), 0);
}

TEST_F(ListWidget, PositionColorsDefaultToDeviceButtonsNotRowOverrides) {
    strlcpy(list_test_defaults.bg_color, "#eeeeee", sizeof(list_test_defaults.bg_color));
    strlcpy(list_test_defaults.fg_color, "#123456", sizeof(list_test_defaults.fg_color));
    use_scroll_viewport("", "", "#000000");
    auto* state = list_widget_impl::get(&tile.widget_state);
    EXPECT_EQ(lv_color_to_u32(lv_obj_get_style_bg_color(row(0), LV_PART_MAIN)) & 0xffffff, 0u);
    EXPECT_EQ(lv_color_to_u32(lv_obj_get_style_bg_color(state->indicator, LV_PART_MAIN)) & 0xffffff, 0xeeeeeeu);
    EXPECT_EQ(lv_color_to_u32(lv_obj_get_style_bg_color(state->indicator, LV_PART_INDICATOR)) & 0xffffff, 0x123456u);
}
