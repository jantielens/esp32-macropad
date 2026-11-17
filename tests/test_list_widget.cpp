#include <gtest/gtest.h>
#include <vector>
#include "widgets/list_widget.cpp"
#include "widgets/text_reader_widget.cpp"
#include "item_binding_scheme.h"

void widget_register(const WidgetType*) {}
void display_manager_lock_if_needed(bool* locked) { *locked = false; }
void display_manager_request_binding_refresh() {}
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
unsigned item_count = 3;
const char* first_title = "first";
uint32_t source_revision() { return revision; }
ListSourceLoadStep load(void**, ListSourceSnapshot* snapshot, const char** error) {
    JsonDocument item;
    unsigned index = 0;
    for (const char* id : {"first", "second", "third", "fourth", "fifth", "sixth"}) {
        if (index++ >= item_count) break;
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
        item_count = 3;
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
        json["widget_list_show_navigation"] = false;
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
        if (!json.containsKey("widget_list_show_navigation")) json["widget_list_show_navigation"] = false;
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
    void use_navigation(unsigned slots = 5) {
        item_count = 6;
        ++revision;
        JsonDocument json;
        json["widget_list_viewport"] = "paged";
        json["widget_list_visible_items"] = slots;
        json["widget_list_show_navigation"] = true;
        reconfigure(json.as<JsonObject>());
    }
};

TEST_F(ListWidget, NavigationReservesSlotsAndPreservesItemActionsAndSelection) {
    use_navigation();
    auto* state = list_widget_impl::get(&tile.widget_state);
    ASSERT_TRUE(state->navigation_shown);
    ASSERT_EQ(state->capacity, 3);
    const unsigned up = state->count, down = up + 1;
    lv_obj_update_layout(button);
    EXPECT_STREQ(lv_label_get_text(lv_obj_get_child(row(up), 0)), LV_SYMBOL_UP);
    EXPECT_STREQ(lv_label_get_text(lv_obj_get_child(row(down), 0)), LV_SYMBOL_DOWN);
    EXPECT_LT(lv_obj_get_y(row(up)), lv_obj_get_y(row(0)));
    EXPECT_GT(lv_obj_get_y(row(down)), lv_obj_get_y(row(2)));
    EXPECT_TRUE(lv_obj_has_state(row(up), LV_STATE_DISABLED));
    tap(up);
    EXPECT_TRUE(feedback_events.empty());

    tap(0);
    ASSERT_EQ(activated_id, "first");
    const auto selected = state->focus;
    feedback_events.clear();
    const int32_t down_y = lv_obj_get_y(row(down));
    tap(down);
    EXPECT_EQ(state->page, 1);
    EXPECT_EQ(state->focus, selected);
    EXPECT_EQ(activations, 1u);
    ASSERT_EQ(feedback_events.size(), 3u);
    EXPECT_EQ(feedback_events[0].event, LV_EVENT_PRESSED);
    EXPECT_EQ(feedback_events[1].event, LV_EVENT_RELEASED);
    EXPECT_EQ(feedback_events[2].event, LV_EVENT_CLICKED);
    EXPECT_TRUE(feedback_events[2].use_default_tap_audio);
    EXPECT_TRUE(lv_obj_has_flag(row(0), LV_OBJ_FLAG_HIDDEN));
    EXPECT_FALSE(lv_obj_has_flag(row(3), LV_OBJ_FLAG_HIDDEN));
    EXPECT_TRUE(lv_obj_has_state(row(down), LV_STATE_DISABLED));
    EXPECT_EQ(lv_obj_get_y(row(down)), down_y);
    feedback_events.clear();
    tap(down);
    EXPECT_TRUE(feedback_events.empty());
    EXPECT_EQ(state->page, 1);

    tap(up, true);
    EXPECT_EQ(state->page, 0);
    EXPECT_EQ(activations, 1u);
    ASSERT_EQ(feedback_events.size(), 3u);
    EXPECT_EQ(feedback_events.back().event, LV_EVENT_CLICKED);
    EXPECT_TRUE(feedback_events.back().use_default_tap_audio);
    tap(1);
    EXPECT_EQ(activated_id, "second");
    EXPECT_FALSE(feedback_events.back().use_default_tap_audio);
}

TEST_F(ListWidget, NavigationCancelsStalePressAndReleasesSlotsWhenItemsFit) {
    use_navigation();
    auto* state = list_widget_impl::get(&tile.widget_state);
    const unsigned down = state->count + 1;
    press(down);
    lv_obj_send_event(row(down), LV_EVENT_PRESS_LOST, input);
    pressed = false;
    lv_indev_read(input);
    EXPECT_EQ(state->page, 0);
    EXPECT_EQ(feedback_events.back().event, LV_EVENT_PRESS_LOST);
    feedback_events.clear();

    press(down);
    first_title = "changed";
    ++revision;
    list_source_loop();
    list_widget_impl::tick(button, &config, &tile.widget_state);
    pressed = false;
    lv_indev_read(input);
    EXPECT_EQ(state->page, 0);
    EXPECT_EQ(feedback_events.back().event, LV_EVENT_PRESS_LOST);

    EXPECT_TRUE(list_widget_command(&tile.widget_state, "next_page"));
    EXPECT_EQ(state->page, 1);
    omit_second = true;
    ++revision;
    list_source_loop();
    list_widget_impl::tick(button, &config, &tile.widget_state);
    EXPECT_EQ(state->count, 5);
    EXPECT_FALSE(state->navigation_shown);
    EXPECT_EQ(state->capacity, 5);
    EXPECT_EQ(state->page, 0);
    EXPECT_TRUE(lv_obj_has_flag(row(state->count), LV_OBJ_FLAG_HIDDEN));
    EXPECT_TRUE(lv_obj_has_flag(row(state->count + 1), LV_OBJ_FLAG_HIDDEN));
    EXPECT_FALSE(lv_obj_has_flag(row(4), LV_OBJ_FLAG_HIDDEN));
    EXPECT_EQ(activations, 0u);
}

TEST_F(ListWidget, AutoNavigationKeepsAnItemSlotAndRecoversAfterTinyLayout) {
    use_navigation(0);
    auto* state = list_widget_impl::get(&tile.widget_state);
    EXPECT_TRUE(state->navigation_shown);
    EXPECT_GE(state->slots, 3);
    EXPECT_GE(state->capacity, 1);
    resize_and_tick(1);
    EXPECT_FALSE(state->layout_ok);
    EXPECT_FALSE(list_widget_command(&tile.widget_state, "next_page"));
    EXPECT_FALSE(lv_obj_has_flag(state->message, LV_OBJ_FLAG_HIDDEN));
    resize_and_tick(300);
    EXPECT_TRUE(state->layout_ok);
    EXPECT_FALSE(state->navigation_shown);
    EXPECT_EQ(state->capacity, 6);
    EXPECT_TRUE(lv_obj_has_flag(state->message, LV_OBJ_FLAG_HIDDEN));
}

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

namespace {
std::string reader_text;
unsigned reader_references = 0;
bool reader_available = true;
BindingResolverStatus reader_scalar(const char*, char* out, size_t size) {
    if (!reader_available) return BINDING_RESOLVER_UNAVAILABLE;
    strlcpy(out, reader_text.c_str(), size);
    return BINDING_RESOLVER_RESOLVED;
}
BindingResolverStatus reader_acquire(const char* key, BindingTextView* out) {
    if (strcmp(key, "text")) return BINDING_RESOLVER_UNKNOWN;
    if (!reader_available) return BINDING_RESOLVER_UNAVAILABLE;
    ++reader_references;
    *out = {reader_text.c_str(), &reader_text, [](void*) { --reader_references; }};
    return BINDING_RESOLVER_RESOLVED;
}
}

class TextReader : public testing::Test {
protected:
    lv_display_t* display = nullptr;
    lv_indev_t* input = nullptr;
    lv_obj_t* parent = nullptr;
    WidgetState runtime{};
    WidgetConfig config{};
    lv_point_t point{};
    std::vector<WidgetPressFeedback> feedback_events;

    void SetUp() override {
        if (!lv_is_initialized()) lv_init();
        static bool registered = false;
        if (!registered) {
            pad_binding_init();
            BindingSchemeSpec spec{1, 1, 1, -1, BINDING_VALIDATION_STANDARD, true};
            spec.acquire_text = reader_acquire;
            ASSERT_TRUE(binding_template_register("reader_test", reader_scalar, nullptr, spec));
            registered = true;
        }
        reader_available = true;
        list_test_defaults = {};
        reader_references = 0;
        reader_text.clear();
        for (int i = 0; i < 80; ++i) reader_text += "A long UTF-8 line \xe2\x82\xac for reading.\n";
        display = lv_display_create(320, 240);
        parent = lv_obj_create(lv_screen_active());
        lv_obj_add_event_cb(lv_obj_get_parent(parent), [](lv_event_t* event) {
            auto* self = static_cast<TextReader*>(lv_event_get_user_data(event));
            self->feedback_events.push_back(*static_cast<const WidgetPressFeedback*>(lv_event_get_param(event)));
        }, LV_EVENT_VALUE_CHANGED, this);
        lv_obj_remove_style_all(parent);
        lv_obj_set_size(parent, 220, 180);
        input = lv_indev_create();
        lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(input, [](lv_indev_t*, lv_indev_data_t* data) {
            data->state = LV_INDEV_STATE_RELEASED;
        });
        configure();
    }
    void configure(bool paged = true, bool navigation = true, const char* track = "", const char* indicator = "") {
        text_reader_impl::destroy(&runtime);
        JsonDocument json;
        json["widget_text_reader_viewport"] = paged ? "paged" : "scroll";
        json["widget_text_reader_show_navigation"] = navigation;
        json["widget_text_reader_indicator_track_color"] = track;
        json["widget_text_reader_indicator_color"] = indicator;
        text_reader_impl::parse(json.as<JsonObject>(), config.data);
        strlcpy(config.data_binding[0], "[reader_test:text|Unavailable]", sizeof(config.data_binding[0]));
        text_reader_impl::create(parent, &config, nullptr, nullptr, &pad_get_scale_info(), &runtime);
        text_reader_impl::show(&runtime);
        tick();
    }
    void tick() {
        lv_obj_update_layout(parent);
        text_reader_impl::tick(parent, &config, &runtime);
        lv_obj_update_layout(parent);
    }
    void tap(int index) {
        auto* state = text_reader_impl::get(&runtime);
        lv_area_t bounds;
        lv_obj_get_coords(state->controls[index], &bounds);
        // The real pointer device supplies coordinates to both navigation events.
        point = {(bounds.x1 + bounds.x2) / 2, (bounds.y1 + bounds.y2) / 2};
        lv_indev_set_user_data(input, this);
        lv_indev_set_read_cb(input, [](lv_indev_t* device, lv_indev_data_t* data) {
            auto* self = static_cast<TextReader*>(lv_indev_get_user_data(device));
            data->point = self->point;
            data->state = LV_INDEV_STATE_RELEASED;
        });
        lv_indev_read(input);
        lv_obj_send_event(state->controls[index], LV_EVENT_PRESSED, input);
        lv_obj_send_event(state->controls[index], LV_EVENT_RELEASED, input);
        lv_obj_update_layout(parent);
    }
    void TearDown() override {
        text_reader_impl::destroy(&runtime);
        pad_binding_set_bindings(nullptr, 0);
        EXPECT_EQ(reader_references, 0u);
        lv_indev_delete(input);
        lv_display_delete(display);
    }
};

TEST_F(TextReader, FullTextIsBorrowedAndPagesCoverWholeLinesWithoutResettingOnTick) {
    auto* state = text_reader_impl::get(&runtime);
    ASSERT_TRUE(state);
    ASSERT_GT(reader_text.size(), BINDING_TEMPLATE_MAX_LEN);
    EXPECT_EQ(lv_label_get_text(state->label), reader_text.c_str());
    EXPECT_EQ(reader_references, 1u);
    ASSERT_TRUE(state->navigation);
    EXPECT_FALSE(lv_obj_has_flag(state->indicator, LV_OBJ_FLAG_HIDDEN));
    EXPECT_EQ(lv_bar_get_value(state->indicator), 10000);
    EXPECT_GT(lv_bar_get_start_value(state->indicator), 0);
    EXPECT_EQ(state->page_height % lv_font_get_line_height(state->font), 0);
    int32_t covered = 0;
    while (state->page + 1 < state->pages) {
        const int32_t page = state->page;
        tap(1);
        ASSERT_EQ(state->page, page + 1);
        EXPECT_EQ(lv_obj_get_y(state->label), -state->page * state->page_height);
        covered += state->page_height;
    }
    EXPECT_GE(covered + state->page_height, state->text_height);
    EXPECT_EQ(lv_bar_get_start_value(state->indicator), 0);
    EXPECT_LT(lv_bar_get_value(state->indicator), 10000);
    EXPECT_TRUE(lv_obj_has_state(state->controls[1], LV_STATE_DISABLED));
    const int32_t last = state->page;
    tap(1);
    tick();
    EXPECT_EQ(state->page, last);
    EXPECT_EQ(reader_references, 1u);
    tap(0);
    EXPECT_EQ(state->page, last - 1);
}

TEST_F(TextReader, NavigationUsesDefaultTapFeedbackOnlyForValidReleases) {
    auto* state = text_reader_impl::get(&runtime);
    tap(0);
    EXPECT_TRUE(feedback_events.empty());

    tap(1);
    tap(0);
    ASSERT_EQ(feedback_events.size(), 2u);
    for (unsigned i = 0; i < feedback_events.size(); ++i) {
        EXPECT_EQ(feedback_events[i].event, LV_EVENT_CLICKED);
        EXPECT_EQ(feedback_events[i].object, state->controls[1 - i]);
        EXPECT_TRUE(feedback_events[i].use_default_tap_audio);
    }

    feedback_events.clear();
    lv_area_t bounds;
    lv_obj_get_coords(state->controls[1], &bounds);
    point = {(bounds.x1 + bounds.x2) / 2, (bounds.y1 + bounds.y2) / 2};
    lv_indev_read(input);
    lv_obj_send_event(state->controls[1], LV_EVENT_PRESSED, input);
    lv_obj_send_event(state->controls[1], LV_EVENT_PRESS_LOST, input);
    lv_obj_send_event(state->controls[1], LV_EVENT_RELEASED, input);
    EXPECT_TRUE(feedback_events.empty());
    EXPECT_EQ(state->page, 0);
}

TEST_F(TextReader, FirstLayoutPlacesNavigationInsidePendingParentSize) {
    text_reader_impl::show(&runtime);
    lv_obj_set_height(parent, 120);
    text_reader_impl::tick(parent, &config, &runtime);
    lv_obj_update_layout(parent);

    auto* state = text_reader_impl::get(&runtime);
    ASSERT_TRUE(state->navigation);
    ASSERT_FALSE(lv_obj_has_flag(state->controls[1], LV_OBJ_FLAG_HIDDEN));
    lv_area_t content, down;
    lv_obj_get_content_coords(parent, &content);
    lv_obj_get_coords(state->controls[1], &down);
    EXPECT_GE(down.y1, content.y1);
    EXPECT_LE(down.y2, content.y2);

    const int32_t first_y = down.y1;
    tick();
    lv_obj_get_coords(state->controls[1], &down);
    EXPECT_EQ(down.y1, first_y);
}

TEST_F(TextReader, LoadingClearsBorrowedTextAndHideReleasesIt) {
    auto* state = text_reader_impl::get(&runtime);
    tap(1);
    reader_available = false;
    tick();
    EXPECT_EQ(reader_references, 0u);
    EXPECT_STREQ(lv_label_get_text(state->label), "Unavailable");
    EXPECT_EQ(state->page, 0);
    EXPECT_FALSE(state->navigation);
    reader_available = true;
    tick();
    EXPECT_EQ(reader_references, 1u);
    text_reader_impl::hide(&runtime);
    EXPECT_EQ(reader_references, 0u);
    EXPECT_STREQ(lv_label_get_text(state->label), "");
}

TEST_F(TextReader, ScrollModeAndScalarBindingsRemainIndependentOfFullTextSources) {
    configure(false);
    auto* state = text_reader_impl::get(&runtime);
    EXPECT_TRUE(lv_obj_has_flag(state->viewport, LV_OBJ_FLAG_SCROLLABLE));
    EXPECT_FALSE(state->navigation);
    lv_obj_scroll_to_y(state->viewport, 100, LV_ANIM_OFF);
    tick();
    EXPECT_EQ(lv_obj_get_scroll_y(state->viewport), 100);
    strlcpy(config.data_binding[0], "literal text", sizeof(config.data_binding[0]));
    tick();
    EXPECT_STREQ(lv_label_get_text(state->label), "literal text");
    EXPECT_EQ(reader_references, 0u);
    EXPECT_EQ(lv_obj_get_scroll_y(state->viewport), 0);
    BindingTextView view;
    EXPECT_EQ(binding_template_acquire_text("prefix [reader_test:text]", &view), BINDING_RESOLVER_UNKNOWN);
    EXPECT_EQ(reader_references, 0u);
}

TEST_F(TextReader, NavigationCanBeDisabledAndTooSmallViewportRecoversOnResize) {
    configure(true, false);
    auto* state = text_reader_impl::get(&runtime);
    EXPECT_FALSE(state->navigation);
    EXPECT_TRUE(lv_obj_has_flag(state->controls[0], LV_OBJ_FLAG_HIDDEN));
    lv_obj_set_height(parent, 4);
    tick();
    EXPECT_FALSE(lv_obj_has_flag(state->message, LV_OBJ_FLAG_HIDDEN));
    lv_obj_set_height(parent, 180);
    tick();
    EXPECT_TRUE(lv_obj_has_flag(state->message, LV_OBJ_FLAG_HIDDEN));
    EXPECT_FALSE(lv_obj_has_flag(state->viewport, LV_OBJ_FLAG_HIDDEN));
}

TEST_F(TextReader, PagedSwipesWorkWithoutControlsAndCannotBubbleIntoPadActions) {
    configure(true, false);
    auto* state = text_reader_impl::get(&runtime);
    unsigned bubbled = 0;
    lv_obj_add_event_cb(parent, [](lv_event_t* event) {
        ++*static_cast<unsigned*>(lv_event_get_user_data(event));
    }, LV_EVENT_RELEASED, &bubbled);
    lv_indev_set_user_data(input, &point);
    lv_indev_set_read_cb(input, [](lv_indev_t* device, lv_indev_data_t* data) {
        data->point = *static_cast<lv_point_t*>(lv_indev_get_user_data(device));
        data->state = LV_INDEV_STATE_RELEASED;
    });
    const auto swipe = [&](int32_t from, int32_t to) {
        point = {30, from};
        lv_indev_read(input);
        lv_obj_send_event(state->viewport, LV_EVENT_PRESSED, input);
        point.y = to;
        lv_indev_read(input);
        lv_obj_send_event(state->viewport, LV_EVENT_RELEASED, input);
        lv_obj_update_layout(parent);
    };
    swipe(120, 40);
    EXPECT_EQ(state->page, 1);
    swipe(40, 120);
    EXPECT_EQ(state->page, 0);
    swipe(40, 50);
    EXPECT_EQ(state->page, 0);
    EXPECT_EQ(bubbled, 0u);
}

TEST_F(TextReader, RejectsInertActionsAndIncompatibleLabelModes) {
    JsonDocument json;
    json["widget_text_reader_style"] = "mode:scroll";
    EXPECT_NE(text_reader_impl::validate(json.as<JsonObjectConst>()), nullptr);
    json["widget_text_reader_style"] = "font_size:18;align:left;mode:wrap";
    EXPECT_EQ(text_reader_impl::validate(json.as<JsonObjectConst>()), nullptr);
    json["actions"].to<JsonArray>().add<JsonObject>();
    EXPECT_NE(text_reader_impl::validate(json.as<JsonObjectConst>()), nullptr);
    json.remove("actions");
    json["widget_text_reader_indicator_color"] = 42;
    EXPECT_NE(text_reader_impl::validate(json.as<JsonObjectConst>()), nullptr);
    json["widget_text_reader_indicator_color"] = "not a color";
    EXPECT_NE(text_reader_impl::validate(json.as<JsonObjectConst>()), nullptr);
    json["widget_text_reader_indicator_color"] = "[pad:indicator]";
    EXPECT_EQ(text_reader_impl::validate(json.as<JsonObjectConst>()), nullptr);
}

TEST_F(TextReader, HalfSurfaceTapsNavigateWhileCancelledDragsAndOutsideReleasesDoNot) {
    unsigned bubbled = 0;
    lv_obj_add_event_cb(parent, [](lv_event_t* event) {
        ++*static_cast<unsigned*>(lv_event_get_user_data(event));
    }, LV_EVENT_RELEASED, &bubbled);
    lv_indev_set_user_data(input, &point);
    lv_indev_set_read_cb(input, [](lv_indev_t* device, lv_indev_data_t* data) {
        data->point = *static_cast<lv_point_t*>(lv_indev_get_user_data(device));
        data->state = LV_INDEV_STATE_RELEASED;
    });
    for (bool navigation : {true, false}) {
        configure(true, navigation);
        feedback_events.clear();
        auto* state = text_reader_impl::get(&runtime);
        lv_area_t bounds;
        lv_obj_get_coords(state->viewport, &bounds);
        const lv_point_t upper{(bounds.x1 + bounds.x2) / 2, bounds.y1 + 8};
        const lv_point_t lower{upper.x, bounds.y2 - 8};
        const auto send = [&](lv_point_t position, lv_event_code_t event) {
            point = position;
            lv_indev_read(input);
            lv_obj_send_event(state->viewport, event, input);
            lv_obj_update_layout(parent);
        };
        send(upper, LV_EVENT_PRESSED);
        send(upper, LV_EVENT_RELEASED);
        EXPECT_EQ(state->page, 0);
        EXPECT_TRUE(feedback_events.empty());
        send(lower, LV_EVENT_PRESSED);
        send(lower, LV_EVENT_RELEASED);
        EXPECT_EQ(state->page, 1);
        ASSERT_EQ(feedback_events.size(), 1u);
        send(upper, LV_EVENT_PRESSED);
        send(upper, LV_EVENT_RELEASED);
        EXPECT_EQ(state->page, 0);
        ASSERT_EQ(feedback_events.size(), 2u);
        for (const auto& feedback : feedback_events) {
            EXPECT_EQ(feedback.event, LV_EVENT_CLICKED);
            EXPECT_EQ(feedback.object, state->viewport);
            EXPECT_TRUE(feedback.use_default_tap_audio);
        }
        send(lower, LV_EVENT_PRESSED);
        send({lower.x + 25, lower.y}, LV_EVENT_PRESSING);
        send(lower, LV_EVENT_RELEASED);
        EXPECT_EQ(state->page, 0);
        send(lower, LV_EVENT_PRESSED);
        send(lower, LV_EVENT_PRESS_LOST);
        send(lower, LV_EVENT_RELEASED);
        EXPECT_EQ(state->page, 0);
        send(lower, LV_EVENT_PRESSED);
        send({lower.x, bounds.y2 + 1}, LV_EVENT_RELEASED);
        EXPECT_EQ(state->page, 0);
        send(lower, LV_EVENT_PRESSED);
        send(upper, LV_EVENT_PRESSING);
        send(upper, LV_EVENT_RELEASED);
        EXPECT_EQ(state->page, 1);
        EXPECT_EQ(feedback_events.size(), 2u);
    }
    EXPECT_EQ(bubbled, 0u);
}

TEST_F(TextReader, PositionIndicatorFollowsScrollingAndScalarColorsAndDisappearsWhenTextFits) {
    strlcpy(list_test_defaults.bg_color, "#eeeeee", sizeof(list_test_defaults.bg_color));
    strlcpy(list_test_defaults.fg_color, "#123456", sizeof(list_test_defaults.fg_color));
    configure(false);
    auto* state = text_reader_impl::get(&runtime);
    EXPECT_EQ(lv_color_to_u32(lv_obj_get_style_bg_color(state->indicator, LV_PART_MAIN)) & 0xffffff, 0xeeeeeeu);
    EXPECT_EQ(lv_color_to_u32(lv_obj_get_style_bg_color(state->indicator, LV_PART_INDICATOR)) & 0xffffff, 0x123456u);

    PadBinding bindings[] = {{"track", "#112233"}, {"indicator", "#abcdef"}};
    pad_binding_set_bindings(bindings, 2);
    configure(false, true, "[pad:track]", "[pad:indicator]");
    state = text_reader_impl::get(&runtime);
    EXPECT_FALSE(lv_obj_has_flag(state->indicator, LV_OBJ_FLAG_CLICKABLE));
    EXPECT_FALSE(lv_obj_has_flag(state->indicator, LV_OBJ_FLAG_HIDDEN));
    EXPECT_EQ(lv_obj_get_width(state->viewport) + 6, lv_obj_get_content_width(parent));
    EXPECT_EQ(lv_color_to_u32(lv_obj_get_style_bg_color(state->indicator, LV_PART_MAIN)) & 0xffffff, 0x112233u);
    EXPECT_EQ(lv_color_to_u32(lv_obj_get_style_bg_color(state->indicator, LV_PART_INDICATOR)) & 0xffffff, 0xabcdefu);
    strlcpy(bindings[1].value, "#654321", sizeof(bindings[1].value));
    tick();
    EXPECT_EQ(lv_color_to_u32(lv_obj_get_style_bg_color(state->indicator, LV_PART_INDICATOR)) & 0xffffff, 0x654321u);
    EXPECT_EQ(lv_bar_get_value(state->indicator), 10000);
    lv_obj_scroll_to_y(state->viewport, 100, LV_ANIM_OFF);
    EXPECT_LT(lv_bar_get_value(state->indicator), 10000);
    lv_obj_scroll_to_y(state->viewport, state->text_height - state->height, LV_ANIM_OFF);
    EXPECT_EQ(lv_bar_get_start_value(state->indicator), 0);
    lv_obj_scroll_to_y(state->viewport, 0, LV_ANIM_OFF);
    EXPECT_EQ(lv_bar_get_value(state->indicator), 10000);
    strlcpy(config.data_binding[0], "Test 123", sizeof(config.data_binding[0]));
    tick();
    EXPECT_TRUE(lv_obj_has_flag(state->indicator, LV_OBJ_FLAG_HIDDEN));
    EXPECT_EQ(lv_obj_get_width(state->viewport), lv_obj_get_content_width(parent));
    EXPECT_STREQ(lv_label_get_text(state->label), "Test 123");
    lv_obj_set_size(parent, 1, 1);
    tick();
    EXPECT_TRUE(lv_obj_has_flag(state->indicator, LV_OBJ_FLAG_HIDDEN));
}
