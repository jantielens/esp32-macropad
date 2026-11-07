#include "widgets/option_selector_config.h"
#include <esp_heap_caps.h>
#include <gtest/gtest.h>
#include <cstdlib>
#include <string>

namespace {
bool fail_allocation = false;
unsigned allocations = 0;
unsigned frees = 0;
}

void* heap_caps_malloc(size_t size, uint32_t caps) {
    EXPECT_EQ(caps, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (fail_allocation) return nullptr;
    ++allocations;
    return malloc(size);
}

void heap_caps_free(void* value) {
    ++frees;
    free(value);
}

size_t strlcpy(char* dst, const char* src, size_t size) {
    const size_t length = strlen(src);
    if (size) {
        const size_t copy = length < size - 1 ? length : size - 1;
        memcpy(dst, src, copy);
        dst[copy] = '\0';
    }
    return length;
}

TEST(OptionSelectorConfig, PsramFailureDoesNotFallBack) {
    StaticJsonDocument<512> doc;
    doc["widget_option_label"] = "Auto";
    doc["widget_option_value"] = "auto";
    uint8_t data[WIDGET_CONFIG_MAX_BYTES] = {};
    const unsigned before = allocations;
    fail_allocation = true;
    EXPECT_FALSE(option_selector_parse_config(doc.as<JsonObject>(), data));
    fail_allocation = false;
    EXPECT_EQ(allocations, before);
    EXPECT_EQ(option_selector_config(data), nullptr);
}

TEST(OptionSelectorConfig, CopiesRetainConfigurationUntilLastRelease) {
    StaticJsonDocument<1024> doc;
    doc["widget_option_mode"] = "local";
    doc["widget_option_label"] = "[mqtt:fan/name]";
    doc["widget_option_value"] = "auto";
    doc["widget_option_label_5"] = "High";
    doc["widget_option_value_5"] = "high";
    doc["widget_option_fg_color_5"] = "[mqtt:fan/color]";
    uint8_t data[WIDGET_CONFIG_MAX_BYTES] = {};
    ASSERT_TRUE(option_selector_parse_config(doc.as<JsonObject>(), data));
    uint8_t copy[WIDGET_CONFIG_MAX_BYTES];
    memcpy(copy, data, sizeof(copy));
    option_selector_retain_config(copy);
    const unsigned before = frees;
    option_selector_release_config(data);
    EXPECT_EQ(frees, before);
    const OptionSelectorConfig* config = option_selector_config(copy);
    ASSERT_NE(config, nullptr);
    EXPECT_EQ(config->mode, OptionSelectorMode::Local);
    EXPECT_STREQ(config->slots[0].content.label, "[mqtt:fan/name]");
    EXPECT_EQ(config->slots[1].kind, OptionSelectorContent::None);
    EXPECT_STREQ(config->slots[4].value, "high");
    EXPECT_STREQ(config->slots[4].foreground, "[mqtt:fan/color]");
    option_selector_release_config(copy);
    EXPECT_EQ(frees, before + 1);
    EXPECT_EQ(option_selector_config(copy), nullptr);
}

TEST(OptionSelectorConfig, ValidatesIdsAndPartialSlots) {
    StaticJsonDocument<1024> doc;
    doc["widget_option_label"] = "Auto";
    EXPECT_EQ(option_selector_validate_config(doc.as<JsonObjectConst>()), nullptr);
    doc["widget_option_mode"] = "local";
    EXPECT_EQ(option_selector_validate_config(doc.as<JsonObjectConst>()), nullptr);
    doc["widget_option_mode"] = "bound";
    EXPECT_NE(option_selector_validate_config(doc.as<JsonObjectConst>()), nullptr);
    doc["widget_option_mode"] = "none";
    doc["widget_option_value"] = "auto";
    doc["widget_option_label_3"] = "Other";
    doc["widget_option_value_3"] = "AUTO";
    EXPECT_EQ(option_selector_validate_config(doc.as<JsonObjectConst>()), nullptr);
    doc["widget_option_mode"] = "bound";
    EXPECT_NE(option_selector_validate_config(doc.as<JsonObjectConst>()), nullptr);
    doc["widget_option_value_3"] = "not valid";
    EXPECT_NE(option_selector_validate_config(doc.as<JsonObjectConst>()), nullptr);
    doc["widget_option_value_3"] = std::string(64, 'x');
    EXPECT_NE(option_selector_validate_config(doc.as<JsonObjectConst>()), nullptr);
    doc["widget_option_value_3"] = std::string(63, 'x');
    EXPECT_EQ(option_selector_validate_config(doc.as<JsonObjectConst>()), nullptr);
}

TEST(OptionSelectorConfig, ParsesOptionsWithoutValuesInLocalMode) {
    StaticJsonDocument<1024> doc;
    doc["widget_option_mode"] = "local";
    doc["widget_option_label"] = "Auto";
    JsonObject icon = doc.createNestedObject("widget_option_icon_2");
    icon["source_id"] = "s_0123456789abcdef01234567";
    icon["kind"] = "mono";
    icon["catalog_id"] = "material-symbols:fan";
    uint8_t data[WIDGET_CONFIG_MAX_BYTES] = {};
    ASSERT_TRUE(option_selector_parse_config(doc.as<JsonObject>(), data));
    const auto* config = option_selector_config(data);
    ASSERT_NE(config, nullptr);
    EXPECT_EQ(config->slots[0].kind, OptionSelectorContent::Label);
    EXPECT_STREQ(config->slots[0].value, "");
    EXPECT_EQ(config->slots[1].kind, OptionSelectorContent::Icon);
    EXPECT_STREQ(config->slots[1].value, "");
    option_selector_release_config(data);
    doc["widget_option_mode"] = "bound";
    EXPECT_NE(option_selector_validate_config(doc.as<JsonObjectConst>()), nullptr);
    doc["widget_option_value"] = "auto";
    EXPECT_NE(option_selector_validate_config(doc.as<JsonObjectConst>()), nullptr);
    doc["widget_option_value_2"] = "fan";
    EXPECT_EQ(option_selector_validate_config(doc.as<JsonObjectConst>()), nullptr);
    doc.remove("widget_option_label");
    EXPECT_NE(option_selector_validate_config(doc.as<JsonObjectConst>()), nullptr);
}

TEST(OptionSelectorConfig, ParsesInsetAndRejectsOutOfRange) {
    StaticJsonDocument<512> doc;
    uint8_t data[WIDGET_CONFIG_MAX_BYTES] = {};
    ASSERT_TRUE(option_selector_parse_config(doc.as<JsonObject>(), data));
    EXPECT_EQ(option_selector_config(data)->option_inset, OPTION_SELECTOR_DEFAULT_INSET);
    option_selector_release_config(data);
    doc["widget_option_inset"] = 0;
    ASSERT_TRUE(option_selector_parse_config(doc.as<JsonObject>(), data));
    EXPECT_EQ(option_selector_config(data)->option_inset, 0);
    option_selector_release_config(data);
    doc["widget_option_inset"] = OPTION_SELECTOR_MAX_INSET + 1;
    EXPECT_NE(option_selector_validate_config(doc.as<JsonObjectConst>()), nullptr);
}

TEST(OptionSelectorConfig, VisitsBoundFieldsAndStableIconSlots) {
    StaticJsonDocument<1024> doc;
    doc["widget_option_group_color"] = "[mqtt:group]";
    doc["widget_option_background_color"] = "[mqtt:background]";
    doc["widget_option_label_2"] = "[mqtt:label]";
    doc["widget_option_value_2"] = "two";
    JsonObject icon = doc.createNestedObject("widget_option_icon_5");
    icon["source_id"] = "s_0123456789abcdef01234567";
    icon["kind"] = "mono";
    icon["catalog_id"] = "material-symbols:fan";
    doc["widget_option_value_5"] = "five";
    uint8_t data[WIDGET_CONFIG_MAX_BYTES] = {};
    ASSERT_TRUE(option_selector_parse_config(doc.as<JsonObject>(), data));
    struct Found { bool group; bool background; bool label; bool icon; } found{};
    option_selector_visit_resources(data,
        [](WidgetResourceKind kind, const char* field, uint8_t slot, const char* value, void* context) {
            auto& found = *static_cast<Found*>(context);
            if (kind == WidgetResourceKind::Binding && !strcmp(field, "widget_option_group_color"))
                found.group = !strcmp(value, "[mqtt:group]");
            if (kind == WidgetResourceKind::Binding && !strcmp(field, "widget_option_background_color"))
                found.background = !strcmp(value, "[mqtt:background]");
            if (kind == WidgetResourceKind::Binding && !strcmp(field, "widget_option_label_2"))
                found.label = slot == 2 && !strcmp(value, "[mqtt:label]");
            if (kind == WidgetResourceKind::MonoIcon && !strcmp(field, "widget_option_icon_5"))
                found.icon = slot == 5 && !strcmp(value, "s_0123456789abcdef01234567");
        }, &found);
    EXPECT_TRUE(found.group);
    EXPECT_TRUE(found.background);
    EXPECT_TRUE(found.label);
    EXPECT_TRUE(found.icon);
    option_selector_release_config(data);
}
