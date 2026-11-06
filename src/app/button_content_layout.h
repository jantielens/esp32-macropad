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
    Right
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
};

struct ButtonContentLayout {
    ButtonContentRect labels[3];
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

inline int32_t button_layout_group_extent(const ButtonContentLayoutInput& input, bool horizontal) {
    int32_t label_width = 0;
    int32_t label_height = 0;
    bool has_label = false;
    for (int i = 0; i < 3; ++i) {
        if (!input.has_label[i]) continue;
        has_label = true;
        const int32_t width = button_layout_nonnegative(input.label_width[i]);
        if (width > label_width) label_width = width;
        label_height += button_layout_nonnegative(input.label_height[i]);
    }
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

inline void button_layout_group(const ButtonContentLayoutInput& input,
                                ButtonContentRect area, bool labels[3],
                                ButtonContentLayout& output) {
    int32_t requested = 0;
    int32_t requested_width = 0;
    int count = 0;
    for (int i = 0; i < 3; ++i) {
        if (!labels[i]) continue;
        requested += button_layout_nonnegative(input.label_height[i]);
        if (input.label_width[i] > requested_width) requested_width = input.label_width[i];
        ++count;
    }
    ButtonContentRect label_area = area;
    if (input.has_icon && count) {
        if (input.icon_position == ButtonGroupIconPosition::Above ||
                   input.icon_position == ButtonGroupIconPosition::Below) {
            const int32_t gap = button_layout_min(button_layout_nonnegative(input.icon_label_gap),
                                                   button_layout_nonnegative(area.height));
            const int32_t icon_height = button_layout_min(input.icon_height,
                button_layout_nonnegative(area.height - gap - requested));
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

    int32_t remaining_height = label_area.height;
    int32_t remaining_requested = requested;
    int32_t y = label_area.y + button_layout_nonnegative(label_area.height - requested) / 2;
    for (int i = 0; i < 3; ++i) {
        if (!labels[i]) continue;
        const int32_t wanted = button_layout_nonnegative(input.label_height[i]);
        const int32_t height = remaining_requested > remaining_height && remaining_requested > 0
            ? remaining_height * wanted / remaining_requested
            : button_layout_min(wanted, remaining_height);
        output.labels[i] = {label_area.x, y, label_area.width, height};
        y += height;
        remaining_height -= height;
        remaining_requested -= wanted;
    }
}

inline ButtonContentLayout button_content_layout(const ButtonContentLayoutInput& input) {
    ButtonContentLayout output{};
    ButtonContentRect bounds = input.bounds;
    bounds.width = button_layout_nonnegative(bounds.width);
    bounds.height = button_layout_nonnegative(bounds.height);
    ButtonContentRect group = bounds;
    bool labels[] = {input.has_label[0], input.has_label[1], input.has_label[2]};
    const bool has_group = input.has_icon || labels[0] || labels[1] || labels[2];

    if (input.has_widget && input.centered_group) {
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
