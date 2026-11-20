#pragma once

#include "pad_config.h"
#include <ArduinoJson.h>
#include <string.h>

// Shared by the runtime loader and JSON authoring validation.
class PadTemplateSelection {
public:
    PadTemplateSelection(int cols, int rows, size_t local_count)
        : cols_(cols), rows_(rows), count_(local_count) {}

    void occupy(int col, int row, int width, int height) {
        for (int c = col; c < col + width && c < MAX_GRID_COLS; ++c)
            for (int r = row; r < row + height && r < MAX_GRID_ROWS; ++r)
                if (c >= 0 && r >= 0) occupied_[c][r] = true;
    }

    // 1 = selected, 0 = hidden, -1 = combined button capacity exceeded.
    int select(int col, int row, int width, int height) {
        if (col < 0 || row < 0 || width < 1 || height < 1 ||
            col + width > cols_ || row + height > rows_ ||
            col + width > MAX_GRID_COLS || row + height > MAX_GRID_ROWS) return 0;
        for (int c = col; c < col + width; ++c)
            for (int r = row; r < row + height; ++r)
                if (occupied_[c][r]) return 0;
        if (count_ >= MAX_PAD_BUTTONS) return -1;
        ++count_;
        occupy(col, row, width, height);
        return 1;
    }

private:
    bool occupied_[MAX_GRID_COLS][MAX_GRID_ROWS] = {};
    int cols_;
    int rows_;
    size_t count_;
};

inline bool pad_binding_name_valid(const char* name) {
    if (!name || !((*name >= 'a' && *name <= 'z') || (*name >= 'A' && *name <= 'Z'))) return false;
    for (const char* p = name + 1; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '_')) return false;
    return strlen(name) < PAD_BINDING_NAME_MAX_LEN;
}

inline void pad_binding_add(PadBinding* bindings, uint8_t& count,
                            const char* name, const char* value) {
    if (!pad_binding_name_valid(name) || !value || count >= PAD_MAX_BINDINGS) return;
    for (uint8_t index = 0; index < count; ++index)
        if (!strcmp(bindings[index].name, name)) return;
    strlcpy(bindings[count].name, name, sizeof(bindings[count].name));
    strlcpy(bindings[count].value, value, sizeof(bindings[count].value));
    ++count;
}

struct PadComposedContext {
    PadBinding bindings[PAD_MAX_BINDINGS] = {};
    JsonObjectConst buttons[MAX_PAD_BUTTONS];
    uint8_t binding_count = 0;
    uint8_t button_count = 0;
};

inline const char* pad_compose_context(JsonObjectConst pad, JsonObjectConst template_pad,
                                        PadComposedContext& output) {
    output.binding_count = output.button_count = 0;
    for (JsonObjectConst values : {pad["bindings"].as<JsonObjectConst>(),
                                   template_pad["bindings"].as<JsonObjectConst>()}) {
        for (JsonPairConst pair : values) {
            const char* name = pair.key().c_str();
            if (!pad_binding_name_valid(name) || !pair.value().is<const char*>()) continue;
            const char* value = pair.value().as<const char*>();
            if (strlen(value) >= CONFIG_LABEL_MAX_LEN) return "binding value too long";
            pad_binding_add(output.bindings, output.binding_count, name, value);
        }
    }
    JsonArrayConst main = pad["buttons"];
    JsonArrayConst overlay = pad["overlay_buttons"];
    if (main.size() + overlay.size() > MAX_PAD_BUTTONS) return "too many buttons across layers";
    PadTemplateSelection selection(pad["cols"] | 3, pad["rows"] | 3, main.size() + overlay.size());
    for (JsonObjectConst button : main) {
        output.buttons[output.button_count++] = button;
        selection.occupy(button["col"] | 0, button["row"] | 0,
                         button["col_span"] | 1, button["row_span"] | 1);
    }
    for (JsonObjectConst button : template_pad["buttons"].as<JsonArrayConst>()) {
        const int selected = selection.select(button["col"] | 0, button["row"] | 0,
                                              button["col_span"] | 1, button["row_span"] | 1);
        if (selected < 0) return "template buttons exceed capacity";
        if (selected) output.buttons[output.button_count++] = button;
    }
    for (JsonObjectConst button : overlay) output.buttons[output.button_count++] = button;
    return nullptr;
}
