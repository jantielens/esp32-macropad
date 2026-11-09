#ifndef BUTTON_CONTENT_LAYOUT_H
#define BUTTON_CONTENT_LAYOUT_H

#include <stdint.h>
#include <string.h>

enum class ButtonGroupIconPosition : uint8_t {
    Above,
    Below,
    Left,
    Right
};

enum class ButtonWidgetPosition : uint8_t {
    Above,
    Below,
    Left,
    Right,
    OverlayTopLeft,
    OverlayTopCenter,
    OverlayTopRight,
    OverlayCenterLeft,
    OverlayCenter,
    OverlayCenterRight,
    OverlayBottomLeft,
    OverlayBottomCenter,
    OverlayBottomRight
};

inline bool button_position_parse(const char* name, uint8_t* position) {
    const char* names[] = {"above", "below", "left", "right"};
    for (uint8_t index = 0; index < 4; ++index) {
        if (strcmp(name, names[index]) == 0) {
            *position = index;
            return true;
        }
    }
    return false;
}

inline bool button_widget_position_parse(const char* name, uint8_t* position) {
    if (button_position_parse(name, position)) return true;
    const char* names[] = {"overlay-top-left", "overlay-top-center", "overlay-top-right",
                           "overlay-center-left", "overlay-center", "overlay-center-right",
                           "overlay-bottom-left", "overlay-bottom-center", "overlay-bottom-right"};
    for (uint8_t index = 0; index < 9; ++index) {
        if (strcmp(name, names[index]) == 0) {
            *position = static_cast<uint8_t>(ButtonWidgetPosition::OverlayTopLeft) + index;
            return true;
        }
    }
    return false;
}

inline bool button_widget_position_is_overlay(ButtonWidgetPosition position) {
    return position >= ButtonWidgetPosition::OverlayTopLeft &&
           position <= ButtonWidgetPosition::OverlayBottomRight;
}

struct ButtonContentRect {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
};

struct ButtonContentLayoutInput {
    ButtonContentRect bounds;
    ButtonWidgetPosition widget_position;
    ButtonGroupIconPosition icon_position;
    uint8_t group_size_pct;
    bool has_widget;
    bool has_icon;
    bool has_label[3];
    int32_t label_height[3];
    int32_t icon_width;
    int32_t icon_height;
    int32_t label_width[3];
    bool centered_group;
    bool center_horizontal;
    int32_t center_start;
    int32_t center_end;
    int32_t icon_label_gap;
    bool reserve_label_slots;
    int32_t label_optical_offset[3];
    int32_t label_ink_top[3];
    int32_t label_ink_bottom_inset[3];
};

struct ButtonContentLayout {
    ButtonContentRect labels[3];
    int32_t label_origin_y[3];
    ButtonContentRect icon;
    ButtonContentRect widget;
};

inline int32_t button_layout_min(int32_t a, int32_t b) {
    return a < b ? a : b;
}

inline int32_t button_layout_nonnegative(int32_t value) {
    return value > 0 ? value : 0;
}

inline ButtonContentRect button_layout_band(ButtonContentRect region, int32_t start, int32_t end,
                                            bool horizontal) {
    const int32_t span = horizontal ? region.width : region.height;
    start = button_layout_min(span, button_layout_nonnegative(start));
    end = button_layout_min(span, button_layout_nonnegative(end));
    if (horizontal) {
        region.x += start;
        region.width = button_layout_nonnegative(end - start);
    } else {
        region.y += start;
        region.height = button_layout_nonnegative(end - start);
    }
    return region;
}

inline int32_t button_layout_gap(int32_t available) {
    return button_layout_min(4, button_layout_nonnegative(available));
}

inline int32_t button_layout_icon_badge_size(int32_t width, int32_t height, int32_t padding) {
    const int32_t size = width > height ? width : height;
    if (size <= 0) return 0;
    return size + 2 * padding;
}

inline int32_t button_layout_ink_top(const ButtonContentLayoutInput& input, int index) {
    return button_layout_min(button_layout_nonnegative(input.label_ink_top[index]),
                             button_layout_nonnegative(input.label_height[index]));
}

inline int32_t button_layout_ink_bottom(const ButtonContentLayoutInput& input, int index) {
    const int32_t height = button_layout_nonnegative(input.label_height[index]);
    const int32_t bottom = height - button_layout_min(height,
        button_layout_nonnegative(input.label_ink_bottom_inset[index]));
    const int32_t top = button_layout_ink_top(input, index);
    return bottom > top ? bottom : top;
}

inline int32_t button_layout_ink_height(const ButtonContentLayoutInput& input, int index) {
    return button_layout_ink_bottom(input, index) - button_layout_ink_top(input, index);
}

inline int32_t button_layout_anchor_radius(const ButtonContentLayoutInput& input, int index) {
    const int32_t center = button_layout_nonnegative(input.label_height[index]) / 2 -
                           input.label_optical_offset[index];
    const int32_t above = center - button_layout_ink_top(input, index);
    const int32_t below = button_layout_ink_bottom(input, index) - center;
    return button_layout_nonnegative(above > below ? above : below);
}

static constexpr int32_t BUTTON_LABEL_GAP = 2;

inline int32_t button_layout_label_extent(const ButtonContentLayoutInput& input, const bool labels[3]) {
    const int count = (labels[0] ? 1 : 0) + (labels[1] ? 1 : 0) + (labels[2] ? 1 : 0);
    if (!count) return 0;
    if (input.reserve_label_slots || count == 1) {
        const int anchor = labels[1] ? 1 : (labels[0] ? 0 : 2);
        const int32_t radius = button_layout_anchor_radius(input, anchor);
        int32_t top = -radius;
        int32_t bottom = radius;
        if (anchor == 1 && labels[0]) top -= BUTTON_LABEL_GAP + button_layout_ink_height(input, 0);
        if (anchor == 1 && labels[2]) bottom += BUTTON_LABEL_GAP + button_layout_ink_height(input, 2);
        const int32_t above = button_layout_nonnegative(-top);
        const int32_t below = button_layout_nonnegative(bottom);
        return 2 * (above > below ? above : below);
    }
    int32_t extent = (count - 1) * BUTTON_LABEL_GAP;
    for (int i = 0; i < 3; ++i)
        if (labels[i]) extent += button_layout_ink_height(input, i);
    return extent;
}

inline int32_t button_layout_group_extent(const ButtonContentLayoutInput& input, bool horizontal) {
    int32_t label_width = 0;
    bool has_label = false;
    bool labels[3];
    for (int i = 0; i < 3; ++i) {
        labels[i] = input.has_label[i] || input.reserve_label_slots;
        if (!labels[i]) continue;
        has_label = true;
        const int32_t width = button_layout_nonnegative(input.label_width[i]);
        if (width > label_width) label_width = width;
    }
    const int32_t label_height = button_layout_label_extent(input, labels);
    if (!input.has_icon) return horizontal ? label_width : label_height;
    if (!has_label) return horizontal ? input.icon_width : input.icon_height;
    const bool side = input.icon_position == ButtonGroupIconPosition::Left ||
                      input.icon_position == ButtonGroupIconPosition::Right;
    if (horizontal) {
        return side ? label_width + button_layout_nonnegative(input.icon_label_gap) + input.icon_width
                    : (label_width > input.icon_width ? label_width : input.icon_width);
    }
    return side ? (label_height > input.icon_height ? label_height : input.icon_height)
                : label_height + button_layout_nonnegative(input.icon_label_gap) + input.icon_height;
}

inline ButtonContentRect button_layout_fit_icon(ButtonContentRect area, int32_t width, int32_t height) {
    if (width <= 0 || height <= 0 || area.width <= 0 || area.height <= 0)
        return {area.x, area.y, 0, 0};
    int32_t fitted_width = button_layout_min(width, area.width);
    int32_t fitted_height = height * fitted_width / width;
    if (fitted_height > area.height) {
        fitted_height = area.height;
        fitted_width = width * fitted_height / height;
    }
    return {area.x + (area.width - fitted_width) / 2,
            area.y + (area.height - fitted_height) / 2,
            fitted_width, fitted_height};
}

inline void button_layout_place_label(const ButtonContentLayoutInput& input, int index,
                                      ButtonContentRect area, int32_t origin,
                                      ButtonContentLayout& output) {
    const int32_t end = origin + button_layout_nonnegative(input.label_height[index]);
    const int32_t area_end = area.y + area.height;
    const int32_t top = button_layout_min(area_end, origin > area.y ? origin : area.y);
    const int32_t bottom = button_layout_min(area_end, end > area.y ? end : area.y);
    output.labels[index] = {area.x, top, area.width, button_layout_nonnegative(bottom - top)};
    output.label_origin_y[index] = origin - top;
}

inline void button_layout_group(const ButtonContentLayoutInput& input,
                                ButtonContentRect area, bool labels[3],
                                ButtonContentLayout& output) {
    int32_t requested_width = 0;
    int count = 0;
    for (int i = 0; i < 3; ++i) {
        if (!labels[i]) continue;
        if (input.label_width[i] > requested_width) requested_width = input.label_width[i];
        ++count;
    }
    ButtonContentRect label_area = area;
    if (input.has_icon && count) {
        if (input.icon_position == ButtonGroupIconPosition::Above ||
                   input.icon_position == ButtonGroupIconPosition::Below) {
            const int32_t label_extent = button_layout_label_extent(input, labels);
            const int32_t gap = button_layout_min(button_layout_nonnegative(input.icon_label_gap),
                                                   button_layout_nonnegative(area.height));
            const int32_t icon_height = button_layout_min(input.icon_height,
                button_layout_nonnegative(area.height - gap - label_extent));
            const bool above = input.icon_position == ButtonGroupIconPosition::Above;
            output.icon = button_layout_fit_icon(
                {area.x, above ? area.y : area.y + area.height - icon_height,
                 area.width, icon_height}, input.icon_width, input.icon_height);
            if (above) label_area.y += icon_height + gap;
            label_area.height = button_layout_nonnegative(area.height - icon_height - gap);
        } else {
            int32_t gap = button_layout_min(button_layout_nonnegative(input.icon_label_gap),
                                             button_layout_nonnegative(area.width));
            const int32_t available = button_layout_nonnegative(area.width - gap);
            const int32_t remaining_for_icon = button_layout_nonnegative(available - requested_width);
            const int32_t minimum_icon = button_layout_min(input.icon_width, available / 8);
            const int32_t icon_width = button_layout_min(input.icon_width,
                remaining_for_icon > minimum_icon ? remaining_for_icon : minimum_icon);
            if (!icon_width) gap = 0;
            const bool icon_left = input.icon_position == ButtonGroupIconPosition::Left;
            output.icon = button_layout_fit_icon(
                {icon_left ? area.x : area.x + area.width - icon_width, area.y,
                 icon_width, area.height}, input.icon_width, input.icon_height);
            if (icon_left) label_area.x += icon_width + gap;
            label_area.width = button_layout_nonnegative(area.width - icon_width - gap);
        }
    } else if (input.has_icon) {
        output.icon = button_layout_fit_icon(area, input.icon_width, input.icon_height);
    }

    if (input.reserve_label_slots || count == 1) {
        const int anchor = labels[1] ? 1 : (labels[0] ? 0 : 2);
        const int32_t height = button_layout_nonnegative(input.label_height[anchor]);
        const int32_t offset = height <= label_area.height ? input.label_optical_offset[anchor] : 0;
        const int32_t y = label_area.y + (label_area.height - height) / 2 + offset;
        button_layout_place_label(input, anchor, label_area, y, output);
        const int32_t center = y + height / 2 - input.label_optical_offset[anchor];
        const int32_t radius = button_layout_anchor_radius(input, anchor);
        if (anchor == 1 && labels[0]) {
            const int32_t top_y = center - radius - BUTTON_LABEL_GAP -
                                  button_layout_ink_bottom(input, 0);
            button_layout_place_label(input, 0, label_area, top_y, output);
        }
        if (anchor == 1 && labels[2]) {
            const int32_t bottom_y = center + radius + BUTTON_LABEL_GAP -
                                     button_layout_ink_top(input, 2);
            button_layout_place_label(input, 2, label_area, bottom_y, output);
        }
    } else if (count > 1) {
        const int32_t extent = button_layout_label_extent(input, labels);
        int32_t top = label_area.y + (label_area.height - extent) / 2;
        for (int i = 0; i < 3; ++i) {
            if (!labels[i]) continue;
            button_layout_place_label(input, i, label_area, top - button_layout_ink_top(input, i), output);
            top += button_layout_ink_height(input, i) + BUTTON_LABEL_GAP;
        }
    }
}

inline ButtonContentLayout button_content_layout(const ButtonContentLayoutInput& input) {
    ButtonContentLayout output{};
    ButtonContentRect bounds = input.bounds;
    bounds.width = button_layout_nonnegative(bounds.width);
    bounds.height = button_layout_nonnegative(bounds.height);
    ButtonContentRect group = bounds;
    bool labels[] = {input.has_label[0] || input.reserve_label_slots,
                     input.has_label[1] || input.reserve_label_slots,
                     input.has_label[2] || input.reserve_label_slots};
    const bool has_group = input.has_icon || labels[0] || labels[1] || labels[2];

    if (input.has_widget && !input.centered_group && button_widget_position_is_overlay(input.widget_position)) {
        output.widget = bounds;
        if (has_group) {
            group.width = button_layout_min(bounds.width, button_layout_nonnegative(button_layout_group_extent(input, true)));
            group.height = button_layout_min(bounds.height, button_layout_nonnegative(button_layout_group_extent(input, false)));
            const uint8_t anchor = static_cast<uint8_t>(input.widget_position) -
                                   static_cast<uint8_t>(ButtonWidgetPosition::OverlayTopLeft);
            group.x += (bounds.width - group.width) * (anchor % 3) / 2;
            group.y += (bounds.height - group.height) * (anchor / 3) / 2;
        } else {
            group = {bounds.x, bounds.y, 0, 0};
        }
    } else if (input.has_widget && input.centered_group) {
        output.widget = bounds;
        if (has_group) {
            const int32_t span = input.center_horizontal ? bounds.width : bounds.height;
            const int32_t start = button_layout_min(span, button_layout_nonnegative(input.center_start));
            const int32_t end = button_layout_min(span, button_layout_nonnegative(input.center_end));
            const int32_t lane = button_layout_nonnegative(end - start);
            const int32_t percent = input.group_size_pct < 10 ? 10 :
                                    input.group_size_pct > 90 ? 90 : input.group_size_pct;
            const int32_t requested = input.group_size_pct == 0
                ? button_layout_nonnegative(button_layout_group_extent(input, input.center_horizontal))
                : span * percent / 100;
            const int32_t size = button_layout_min(requested, lane);
            group = button_layout_band(bounds, start + (lane - size) / 2,
                                       start + (lane - size) / 2 + size, input.center_horizontal);
        } else {
            group = {bounds.x, bounds.y, 0, 0};
        }
    } else if (input.has_widget && has_group) {
        const bool horizontal = input.widget_position == ButtonWidgetPosition::Left ||
                                input.widget_position == ButtonWidgetPosition::Right;
        const bool first = input.widget_position == ButtonWidgetPosition::Right ||
                           input.widget_position == ButtonWidgetPosition::Below;
        const int32_t available = horizontal ? bounds.width : bounds.height;
        const int32_t gap = button_layout_gap(available);
        const int32_t remaining = available - gap;
        const int32_t percent = input.group_size_pct < 10 ? 10 :
                                input.group_size_pct > 90 ? 90 : input.group_size_pct;
        const int32_t group_size = input.group_size_pct == 0
            ? button_layout_min(button_layout_nonnegative(button_layout_group_extent(input, horizontal)), remaining / 2)
            : remaining * percent / 100;
        if (horizontal) {
            group.width = group_size;
            group.x = first ? bounds.x : bounds.x + remaining - group_size + gap;
            output.widget = {first ? bounds.x + group_size + gap : bounds.x, bounds.y,
                             remaining - group_size, bounds.height};
        } else {
            group.height = group_size;
            group.y = first ? bounds.y : bounds.y + remaining - group_size + gap;
            output.widget = {bounds.x, first ? bounds.y + group_size + gap : bounds.y,
                             bounds.width, remaining - group_size};
        }
    } else if (input.has_widget) {
        output.widget = bounds;
        group = {bounds.x, bounds.y, 0, 0};
    }
    button_layout_group(input, group, labels, output);
    return output;
}

#endif
