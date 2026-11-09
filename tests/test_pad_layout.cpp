#include <cassert>
#include <cstdio>
#include <initializer_list>

#include "pad_layout.h"
#include "button_content_layout.h"

extern const lv_font_t lv_font_montserrat_12 = {};
extern const lv_font_t lv_font_montserrat_14 = {};
extern const lv_font_t lv_font_montserrat_18 = {};
extern const lv_font_t lv_font_montserrat_24 = {};
extern const lv_font_t lv_font_montserrat_32 = {};
extern const lv_font_t lv_font_montserrat_36 = {};

static void test_layout_spacing_and_insets() {
    const uint8_t positions[] = {0, 1, 0, 1};
    const uint8_t rows[] = {0, 0, 1, 1};
    const uint8_t spans[] = {1, 1, 1, 1};
    PadRect rects[4] = {};
    const PadGridLayoutSpace layout = {8, 12, 4, 16, 6, 6, 4};
    const DisplayContentRect content = display_safe_content_rect(100, 100);

    pad_compute_grid(2, 2, 100, 100, positions, rows,
                     spans, spans, 4, rects, &layout);

    assert(rects[0].x == content.x + layout.pixel_shift_margin + layout.left);
    assert(rects[0].y == content.y + layout.pixel_shift_margin + layout.top);
    assert(rects[1].x == rects[0].x + rects[0].w + layout.gap_x);
    assert(rects[2].y == rects[0].y + rects[0].h + layout.gap_y);
    assert(rects[1].x + rects[1].w + layout.right <= content.x + content.w - layout.pixel_shift_margin);
    assert(rects[2].y + rects[2].h + layout.bottom <= content.y + content.h - layout.pixel_shift_margin);

    const PadGridLayoutSpace no_shift = {0, 0, 0, 0, 0, 0, 0};
    pad_compute_grid(1, 1, 100, 100, positions, rows,
                     spans, spans, 1, rects, &no_shift);
    assert(rects[0].x == content.x);
    assert(rects[0].y == content.y);
    assert(rects[0].w == content.w);
    assert(rects[0].h == content.h);
}

static void test_nonuniform_tracks_spans_and_rounding() {
    const uint8_t columns[] = {0, 2, 3};
    const uint8_t rows[] = {0, 1, 0};
    const uint8_t col_spans[] = {2, 1, 1};
    const uint8_t row_spans[] = {2, 1, 1};
    const uint16_t column_widths[] = {0, 20, 0};
    const uint16_t row_heights[] = {25, 0};
    const PadGridLayoutSpace layout = {0, 0, 0, 0, 2, 0, 0};
    PadRect rects[3] = {};

    pad_compute_grid(3, 2, 101, 100, columns, rows,
                     col_spans, row_spans, 3, rects, &layout,
                     column_widths, row_heights);

    assert(rects[0].x == 0 && rects[0].y == 0);
    assert(rects[0].w == 61 && rects[0].h == 100);
    assert(rects[1].x == 63 && rects[1].y == 25);
    assert(rects[1].w == 38 && rects[1].h == 75);
    assert(rects[2].x == 103 && rects[2].w == 38);

    uint16_t resized_columns[3] = {};
    assert(pad_compute_axis_track_sizes(3, 121, 2, column_widths, resized_columns));
    assert(resized_columns[0] == 49 && resized_columns[1] == 20 && resized_columns[2] == 48);
}

static void test_auto_tracks_fill_screen() {
    const uint8_t columns[] = {0, 1, 7};
    const uint8_t rows[] = {0, 1, 5};
    const uint8_t spans[] = {1, 1, 1};
    const uint8_t full_col_spans[] = {8};
    const uint8_t full_row_spans[] = {6};
    const uint8_t origin[] = {0};
    PadRect rects[3] = {};
    for (uint16_t gap : {0, 6, 10}) {
        const PadGridLayoutSpace layout = {0, 0, 0, 0, gap, gap, 0};
        pad_compute_grid(8, 6, 400, 300, columns, rows, spans, spans, 3, rects, &layout);
        assert(rects[0].x == 0 && rects[0].y == 0);
        assert(rects[1].x == rects[0].x + rects[0].w + gap);
        assert(rects[1].y == rects[0].y + rects[0].h + gap);
        assert(rects[2].x + rects[2].w == 400);
        assert(rects[2].y + rects[2].h == 300);
        pad_compute_grid(8, 6, 400, 300, origin, origin,
                         full_col_spans, full_row_spans, 1, rects, &layout);
        assert(rects[0].w == 400 && rects[0].h == 300);
    }
}

static void test_ten_by_ten_grid() {
    const uint8_t columns[] = {8, 9};
    const uint8_t rows[] = {8, 9};
    const uint8_t spans[] = {2, 1};
    const uint16_t column_widths[] = {0, 0, 0, 0, 0, 0, 0, 0, 20, 0};
    const uint16_t row_heights[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 30};
    const PadGridLayoutSpace layout = {0, 0, 0, 0, 0, 0, 0};
    PadRect rects[2] = {};
    pad_compute_grid(10, 10, 120, 120, columns, rows, spans, spans, 2, rects,
                     &layout, column_widths, row_heights);
    assert(rects[0].x == 89 && rects[0].y == 80);
    assert(rects[0].w == 31 && rects[0].h == 40);
    assert(rects[1].x == 109 && rects[1].y == 90);
    assert(rects[1].w == 11 && rects[1].h == 30);
}

static void test_invalid_track_overrides_fall_back_to_uniform() {
    const uint16_t all_fixed[] = {30, 30, 30};
    uint16_t sizes[3] = {};
    assert(!pad_compute_axis_track_sizes(3, 100, 2, all_fixed, sizes));
    assert(sizes[0] == 32 && sizes[1] == 32 && sizes[2] == 32);

    const uint16_t insufficient_auto[] = {95, 0, 0};
    assert(!pad_compute_axis_track_sizes(3, 100, 2, insufficient_auto, sizes));
    assert(sizes[0] == 32 && sizes[1] == 32 && sizes[2] == 32);
}

static void test_stopwatch_466_square_round_viewport() {
    constexpr uint16_t size = 466;
    uint8_t columns[25], rows[25], spans[25];
    for (uint8_t index = 0; index < 25; ++index) {
        columns[index] = index % 5;
        rows[index] = index / 5;
        spans[index] = 1;
    }
    PadRect rects[25] = {};
    pad_compute_grid(5, 5, size, size, columns, rows, spans, spans, 25, rects);
    for (const PadRect& rect : rects) {
        assert(rect.w > 0 && rect.h > 0);
        assert(rect.x >= DEFAULT_PIXEL_SHIFT_DISTANCE);
        assert(rect.y >= DEFAULT_PIXEL_SHIFT_DISTANCE);
        assert(rect.x + rect.w <= size - DEFAULT_PIXEL_SHIFT_DISTANCE);
        assert(rect.y + rect.h <= size - DEFAULT_PIXEL_SHIFT_DISTANCE);
    }

    const DisplayContentRect full = display_safe_content_rect(size, size);
    assert(full.x == 0 && full.y == 0 && full.w == size && full.h == size);
    assert(rects[0].x == 4 && rects[0].y == 4);
    const uint16_t baseline = (size - 8 - 4 * pad_get_scale_info().gap) / 5;
    const uint16_t remainder = (size - 8 - 4 * pad_get_scale_info().gap) % 5;
    assert(rects[0].w == baseline + (remainder ? 1 : 0));
    assert(rects[0].h == baseline + (remainder ? 1 : 0));
    const PadGridLayoutSpace inscribed = {68, 68, 68, 68, 3, 3, 4};
    pad_compute_grid(5, 5, size, size, columns, rows, spans, spans, 25, rects, &inscribed);
    for (const PadRect& rect : rects) {
        const int corners_x[] = {rect.x, rect.x + rect.w - 1};
        const int corners_y[] = {rect.y, rect.y + rect.h - 1};
        for (int corner_x : corners_x) {
            for (int corner_y : corners_y) {
                for (int shift_x : {-4, 4}) {
                    for (int shift_y : {-4, 4}) {
                        const int dx = 2 * (corner_x + shift_x) - (size - 1);
                        const int dy = 2 * (corner_y + shift_y) - (size - 1);
                        assert(dx * dx + dy * dy <= size * size);
                    }
                }
            }
        }
    }
    const PadGridLayoutSpace settings = {8, 12, 4, 16, 6, 6, 4};
    columns[0] = rows[0] = 0;
    spans[0] = 5;
    pad_compute_grid(5, 5, size, size, columns, rows, spans, spans, 1, rects, &settings);
    assert(rects[0].x == 12 && rects[0].y == 8);
    assert(rects[0].x + rects[0].w <= size - 4 - settings.right);
    assert(rects[0].y + rects[0].h <= size - 4 - settings.bottom);
}

static bool overlaps(const ButtonContentRect& a, const ButtonContentRect& b) {
    return a.width > 0 && a.height > 0 && b.width > 0 && b.height > 0 &&
        a.x < b.x + b.width && b.x < a.x + a.width &&
        a.y < b.y + b.height && b.y < a.y + a.height;
}

static void test_button_content_regions() {
    ButtonContentLayoutInput input = {{0, 0, 200, 100}, ButtonWidgetPosition::Right,
        ButtonGroupIconPosition::Left, 40, true, true, {true, true, true},
        {12, 24, 12}, 30, 30, {40, 60, 30}, false, false, 0, 0, 4, false, {}, {}, {}};
    for (ButtonWidgetPosition position : {ButtonWidgetPosition::Above, ButtonWidgetPosition::Below,
                                         ButtonWidgetPosition::Left, ButtonWidgetPosition::Right}) {
        input.widget_position = position;
        const ButtonContentLayout layout = button_content_layout(input);
        for (const ButtonContentRect& label : layout.labels) {
            assert(!overlaps(label, layout.widget));
            assert(!overlaps(label, layout.icon));
        }
        assert(!overlaps(layout.icon, layout.widget));
        for (int i = 0; i < 3; ++i)
            for (int j = i + 1; j < 3; ++j)
                assert(!overlaps(layout.labels[i], layout.labels[j]));
    }

    input.widget_position = ButtonWidgetPosition::Right;
    ButtonContentLayout left = button_content_layout(input);
    assert(left.widget.x == (200 - 4) * 40 / 100 + 4);
    input.has_icon = false;
    input.has_label[0] = false;
    left = button_content_layout(input);
    assert(left.labels[0].height == 0);
    assert(left.labels[1].y < left.labels[2].y);
    assert(left.icon.width == 0);
    input.bounds = {0, 0, 12, 9};
    input.has_icon = true;
    left = button_content_layout(input);
    assert(!overlaps(left.labels[1], left.widget));
    assert(!overlaps(left.labels[2], left.icon));
    assert(left.widget.x + left.widget.width <= input.bounds.width);
    input.bounds = {0, 0, 200, 100};
    input.has_widget = false;
    input.icon_position = ButtonGroupIconPosition::Below;
    const ButtonContentLayout below = button_content_layout(input);
    assert(below.icon.y >= below.labels[2].y + below.labels[2].height);
    assert(below.icon.height > 0);
}

static void test_label_slots() {
    ButtonContentLayoutInput input = {{0, 0, 200, 100}, ButtonWidgetPosition::Right,
        ButtonGroupIconPosition::Left, 0, false, false, {true, true, true},
        {12, 24, 12}, 0, 0, {30, 60, 30}, false, false, 0, 0, 4, true, {}, {}, {}};
    input.label_optical_offset[1] = -3;
    const auto all = button_content_layout(input);
    assert(all.labels[1].y == 35);
    assert(all.labels[0].y + all.labels[0].height + BUTTON_LABEL_GAP <= all.labels[1].y);
    assert(all.labels[2].y >= all.labels[1].y + all.labels[1].height + BUTTON_LABEL_GAP);
    input.has_label[0] = false;
    input.has_label[2] = false;
    input.reserve_label_slots = false;
    auto one = button_content_layout(input);
    assert(one.labels[1].y == all.labels[1].y);
    assert(one.labels[0].height == 0 && one.labels[2].height == 0);
    input.reserve_label_slots = true;
    one = button_content_layout(input);
    for (int i = 0; i < 3; ++i) {
        assert(one.labels[i].y == all.labels[i].y);
        assert(one.labels[i].height == all.labels[i].height);
    }
    input.reserve_label_slots = false;
    input.has_label[0] = input.has_label[2] = true;
    input.has_label[1] = false;
    const auto pair = button_content_layout(input);
    assert(pair.labels[0].y == 37 && pair.labels[2].y == 51);
    input.has_label[2] = false;
    const auto top_only = button_content_layout(input);
    assert(top_only.labels[0].y == 44);
    input.has_label[1] = input.has_label[2] = true;
    input.bounds.height = 15;
    const auto small = button_content_layout(input);
    for (const auto& label : small.labels) {
        assert(label.y >= 0);
        assert(label.y + label.height <= input.bounds.height);
    }
    assert(!overlaps(small.labels[0], small.labels[1]));
    assert(!overlaps(small.labels[1], small.labels[2]));

    input.bounds.height = 200;
    input.has_widget = true;
    input.widget_position = ButtonWidgetPosition::Below;
    const auto automatic = button_content_layout(input);
    assert(automatic.labels[0].height == 12);
    assert(automatic.labels[1].height == 24);
    assert(automatic.labels[2].height == 12);
}

static void test_label_ink_spacing_and_clipping() {
    ButtonContentLayoutInput input = {{0, 0, 200, 84}, ButtonWidgetPosition::Right,
        ButtonGroupIconPosition::Left, 0, false, false, {true, true, true},
        {30, 50, 30}, 0, 0, {30, 60, 30}, false, false, 0, 0, 4, false,
        {}, {8, 10, 8}, {8, 10, 8}};
    const auto layout = button_content_layout(input);
    auto origin = [&](int index) { return layout.labels[index].y + layout.label_origin_y[index]; };
    assert(overlaps(layout.labels[0], layout.labels[1]));
    assert(overlaps(layout.labels[1], layout.labels[2]));
    assert(origin(0) + button_layout_ink_bottom(input, 0) + BUTTON_LABEL_GAP ==
           origin(1) + button_layout_ink_top(input, 1));
    assert(origin(1) + button_layout_ink_bottom(input, 1) + BUTTON_LABEL_GAP ==
           origin(2) + button_layout_ink_top(input, 2));
    assert(origin(1) + input.label_height[1] / 2 == input.bounds.height / 2);

    input.bounds.height = 20;
    const auto clipped = button_content_layout(input);
    for (const auto& label : clipped.labels) {
        assert(label.y >= 0);
        assert(label.y + label.height <= input.bounds.height);
    }
    assert(clipped.label_origin_y[1] < 0);
}

static void test_compact_label_group() {
    ButtonContentLayoutInput input = {{0, 0, 200, 100}, ButtonWidgetPosition::Below,
        ButtonGroupIconPosition::Left, 0, false, false, {true, true, false},
        {20, 40, 30}, 0, 0, {30, 60, 30}, false, false, 0, 0, 4, false,
        {}, {4, 8, 6}, {4, 8, 6}};
    auto layout = button_content_layout(input);
    auto ink_top = [&](int i) {
        return layout.labels[i].y + layout.label_origin_y[i] + button_layout_ink_top(input, i);
    };
    auto ink_bottom = [&](int i) {
        return layout.labels[i].y + layout.label_origin_y[i] + button_layout_ink_bottom(input, i);
    };
    assert(ink_top(0) + ink_bottom(1) == input.bounds.height);
    assert(ink_bottom(0) + BUTTON_LABEL_GAP == ink_top(1));
    assert(ink_bottom(1) > input.bounds.height / 2);
    assert(layout.labels[2].height == 0);

    input.has_label[0] = false;
    input.has_label[2] = true;
    layout = button_content_layout(input);
    assert(ink_top(1) + ink_bottom(2) == input.bounds.height);
    assert(ink_bottom(1) + BUTTON_LABEL_GAP == ink_top(2));

    input.has_label[0] = true;
    layout = button_content_layout(input);
    assert(ink_top(0) + ink_bottom(2) == input.bounds.height);
    assert(ink_bottom(0) + BUTTON_LABEL_GAP == ink_top(1));
    assert(ink_bottom(1) + BUTTON_LABEL_GAP == ink_top(2));
    assert(ink_top(1) + ink_bottom(1) != input.bounds.height);

    input.bounds.height = 200;
    input.has_widget = true;
    layout = button_content_layout(input);
    assert(layout.widget.y == 12 + 24 + 18 + 2 * BUTTON_LABEL_GAP + 4);
}

static void test_button_content_auto_and_vertical_size() {
    ButtonContentLayoutInput input = {{0, 0, 200, 100}, ButtonWidgetPosition::Right,
        ButtonGroupIconPosition::Left, 0, true, true, {true, true, false},
        {10, 20, 0}, 20, 20, {30, 40, 0}, false, false, 0, 0, 4, false, {}, {}, {}};
    ButtonContentLayout result = button_content_layout(input);
    assert(result.widget.x == 64 + 4);
    input.label_width[1] = 180;
    result = button_content_layout(input);
    assert(result.widget.x == 98 + 4);
    input.widget_position = ButtonWidgetPosition::Below;
    input.icon_position = ButtonGroupIconPosition::Above;
    result = button_content_layout(input);
    assert(result.widget.y == 48 + 4);
    input.group_size_pct = 25;
    result = button_content_layout(input);
    assert(result.widget.y == 24 + 4);
    input.has_icon = false;
    input.has_label[0] = input.has_label[1] = false;
    result = button_content_layout(input);
    assert(result.widget.width == 200 && result.widget.height == 100);
}

static void test_button_content_shrinks_side_icon_for_labels() {
    ButtonContentLayoutInput input = {{0, 0, 200, 100}, ButtonWidgetPosition::Right,
        ButtonGroupIconPosition::Left, 60, true, true, {false, true, false},
        {0, 24, 0}, 60, 60, {0, 90, 0}, false, false, 0, 0, 4, false, {}, {}, {}};
    ButtonContentLayout result = button_content_layout(input);
    assert(result.labels[1].width >= 90);
    assert(result.icon.width > 0 && result.icon.width < 60);
    input.label_width[1] = 200;
    result = button_content_layout(input);
    assert(result.icon.width > 0);
    assert(result.labels[1].width > result.icon.width);
}

static void test_icon_label_gap_follows_padding() {
    ButtonContentLayoutInput input = {{0, 0, 200, 200}, ButtonWidgetPosition::Right,
        ButtonGroupIconPosition::Left, 0, true, true, {false, true, false},
        {0, 20, 0}, 20, 20, {0, 40, 0}, false, false, 0, 0, 0, false, {}, {}, {}};
    for (int gap : {0, 12}) {
        input.icon_label_gap = gap;
        for (auto position : {ButtonGroupIconPosition::Left, ButtonGroupIconPosition::Right}) {
            input.icon_position = position;
            const auto layout = button_content_layout(input);
            assert(layout.widget.x == 20 + 40 + gap + 4);
            if (position == ButtonGroupIconPosition::Left)
                assert(layout.labels[1].x - (layout.icon.x + layout.icon.width) == gap);
            else
                assert(layout.icon.x - (layout.labels[1].x + layout.labels[1].width) == gap);
        }
        input.widget_position = ButtonWidgetPosition::Below;
        for (auto position : {ButtonGroupIconPosition::Above, ButtonGroupIconPosition::Below}) {
            input.icon_position = position;
            const auto layout = button_content_layout(input);
            assert(layout.widget.y == 20 + 20 + gap + 4);
            if (position == ButtonGroupIconPosition::Above)
                assert(layout.labels[1].y - (layout.icon.y + layout.icon.height) == gap);
            else
                assert(layout.icon.y - (layout.labels[1].y + layout.labels[1].height) == gap);
        }
        input.widget_position = ButtonWidgetPosition::Right;
    }
}

static void test_widget_feedback_bands_exclude_content_group() {
    const ButtonContentRect region = {80, 12, 101, 75};
    const ButtonContentRect left = button_layout_band(region, 0, 51, true);
    const ButtonContentRect right = button_layout_band(region, 51, 101, true);
    assert(left.x == 80 && left.width == 51);
    assert(right.x == 131 && right.width == 50);
    assert(left.y == region.y && left.height == region.height);
    assert(right.x + right.width == region.x + region.width);
    const ButtonContentRect lower = button_layout_band(region, 38, 75, false);
    assert(lower.x == region.x && lower.width == region.width);
    assert(lower.y == 50 && lower.height == 37);
    const ButtonContentRect clipped = button_layout_band(region, -10, 120, true);
    assert(clipped.x == region.x && clipped.width == region.width);
}

static void test_centered_group_preserves_widget_region() {
    ButtonContentLayoutInput input = {{0, 0, 200, 100}, ButtonWidgetPosition::Right,
        ButtonGroupIconPosition::Left, 0, true, true, {false, true, false},
        {0, 24, 0}, 20, 20, {0, 40, 0}, false, false, 0, 0, 4, false, {}, {}, {}};
    input.centered_group = true;
    input.center_horizontal = true;
    input.center_start = 60;
    input.center_end = 140;
    ButtonContentLayout layout = button_content_layout(input);
    assert(layout.widget.x == 0 && layout.widget.width == 200);
    assert(layout.icon.x >= 60 && layout.labels[1].x + layout.labels[1].width <= 140);
    input.group_size_pct = 90;
    layout = button_content_layout(input);
    assert(layout.labels[1].x + layout.labels[1].width <= 140);
    input.center_horizontal = false;
    input.center_start = 35;
    input.center_end = 65;
    layout = button_content_layout(input);
    assert(layout.widget.height == 100);
    assert(layout.icon.y >= 35 && layout.labels[1].y + layout.labels[1].height <= 65);
    input.center_start = 51;
    input.center_end = 49;
    layout = button_content_layout(input);
    assert(layout.icon.height == 0 && layout.labels[1].height == 0);
    input.has_icon = input.has_label[1] = false;
    layout = button_content_layout(input);
    assert(layout.widget.width == 200 && layout.widget.height == 100);
}

static void test_overlay_group_anchors() {
    ButtonContentLayoutInput input = {{10, 20, 200, 100}, ButtonWidgetPosition::OverlayTopLeft,
        ButtonGroupIconPosition::Left, 90, true, true, {false, true, false},
        {0, 20, 0}, 20, 20, {0, 40, 0}, false, false, 0, 0, 4, false, {}, {}, {}};
    for (int anchor = 0; anchor < 9; ++anchor) {
        input.widget_position = static_cast<ButtonWidgetPosition>(
            static_cast<int>(ButtonWidgetPosition::OverlayTopLeft) + anchor);
        const auto layout = button_content_layout(input);
        assert(layout.widget.x == 10 && layout.widget.y == 20);
        assert(layout.widget.width == 200 && layout.widget.height == 100);
        const int group_x = 10 + (200 - 64) * (anchor % 3) / 2;
        const int group_y = 20 + (100 - 20) * (anchor / 3) / 2;
        assert(layout.icon.x == group_x && layout.icon.y == group_y);
        assert(layout.labels[1].x == group_x + 24 && layout.labels[1].y == group_y);
        assert(overlaps(layout.labels[1], layout.widget));
    }
    input.bounds = {10, 20, 8, 6};
    const auto small = button_content_layout(input);
    assert(small.widget.width == 8 && small.widget.height == 6);
    assert(small.icon.x >= 10 && small.icon.x + small.icon.width <= 18);
    assert(small.labels[1].y >= 20 && small.labels[1].y + small.labels[1].height <= 26);
    input.has_icon = input.has_label[1] = false;
    const auto empty = button_content_layout(input);
    assert(empty.widget.width == 8 && empty.widget.height == 6);
    assert(empty.icon.width == 0 && empty.labels[1].height == 0);
}

int main() {
    test_layout_spacing_and_insets();
    test_nonuniform_tracks_spans_and_rounding();
    test_auto_tracks_fill_screen();
    test_ten_by_ten_grid();
    test_invalid_track_overrides_fall_back_to_uniform();
    test_stopwatch_466_square_round_viewport();
    test_button_content_regions();
    test_label_slots();
    test_label_ink_spacing_and_clipping();
    test_compact_label_group();
    test_button_content_auto_and_vertical_size();
    test_button_content_shrinks_side_icon_for_labels();
    test_icon_label_gap_follows_padding();
    test_widget_feedback_bands_exclude_content_group();
    test_centered_group_preserves_widget_region();
    test_overlay_group_anchors();
    std::puts("pad_layout: PASS");
    return 0;
}