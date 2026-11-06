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
    assert(rects[0].w == baseline && rects[0].h == baseline);
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
        {12, 24, 12}, 30, 30, {40, 60, 30}};
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

static void test_button_content_auto_and_vertical_size() {
    ButtonContentLayoutInput input = {{0, 0, 200, 100}, ButtonWidgetPosition::Right,
        ButtonGroupIconPosition::Left, 0, true, true, {true, true, false},
        {10, 20, 0}, 20, 20, {30, 40, 0}};
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
        {0, 24, 0}, 60, 60, {0, 90, 0}};
    ButtonContentLayout result = button_content_layout(input);
    assert(result.labels[1].width >= 90);
    assert(result.icon.width > 0 && result.icon.width < 60);
    input.label_width[1] = 200;
    result = button_content_layout(input);
    assert(result.icon.width > 0);
    assert(result.labels[1].width > result.icon.width);
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

int main() {
    test_layout_spacing_and_insets();
    test_nonuniform_tracks_spans_and_rounding();
    test_invalid_track_overrides_fall_back_to_uniform();
    test_stopwatch_466_square_round_viewport();
    test_button_content_regions();
    test_button_content_auto_and_vertical_size();
    test_button_content_shrinks_side_icon_for_labels();
    test_widget_feedback_bands_exclude_content_group();
    std::puts("pad_layout: PASS");
    return 0;
}