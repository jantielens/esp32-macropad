#include "list_source.h"
#include "item_binding.h"
#include "binding_template.h"
#include "expr_binding.h"
#include "pad_binding.h"
#include "pad_composition.h"
#include "list_source_voice_notes.h"
#include "voice_notes.h"
#include "widgets/list_config.h"
#include "action_registry.h"
#include "lvgl_heap.h"
#if TEST_LIST_SOURCE_HEALTH
#include "list_source_health.h"
#include "device_telemetry.h"
#include "wifi_manager.h"
#endif

#include <gtest/gtest.h>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>

namespace {
bool fail_allocations = false;
size_t live_allocations = 0;
size_t non_psram_allocations = 0;
std::recursive_mutex display_mutex;
bool ota_active = false;
unsigned binding_refreshes = 0;
}

void display_manager_request_binding_refresh() { ++binding_refreshes; }

void display_manager_lock_if_needed(bool* locked) {
    display_mutex.lock();
    *locked = true;
}
void display_manager_unlock_if_needed(bool locked) {
    if (locked) display_mutex.unlock();
}
bool ota_activity_is_active() { return ota_active; }

void* heap_caps_malloc(size_t size, uint32_t caps) {
    if (!(caps & MALLOC_CAP_SPIRAM)) ++non_psram_allocations;
    if (fail_allocations && (caps & MALLOC_CAP_SPIRAM)) return nullptr;
    void* result = malloc(size);
    if (result) ++live_allocations;
    return result;
}

void* heap_caps_realloc(void* ptr, size_t size, uint32_t caps) {
    if (!(caps & MALLOC_CAP_SPIRAM)) ++non_psram_allocations;
    if (fail_allocations && (caps & MALLOC_CAP_SPIRAM)) return nullptr;
    void* result = realloc(ptr, size);
    if (result && !ptr) ++live_allocations;
    return result;
}

void heap_caps_free(void* ptr) {
    if (ptr) --live_allocations;
    free(ptr);
}

bool psramFound() { return true; }
size_t heap_caps_get_total_size(uint32_t) { return 1024 * 1024; }
extern "C" void mqtt_sub_store_ensure_binding_subscribed(const char*) {}

class ListSourceTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        item_binding_init();
        expr_binding_init();
    }
    void SetUp() override {
        fail_allocations = false;
        non_psram_allocations = 0;
        binding_refreshes = 0;
        ASSERT_EQ(live_allocations, 0u);
    }
    void TearDown() override {
        fail_allocations = false;
        EXPECT_EQ(non_psram_allocations, 0u);
        EXPECT_EQ(live_allocations, 0u);
    }
};

TEST(ListSourceStartup, RegistersCoreSourcesBeforeLoadingPads) {
    std::ifstream file("src/app/app.ino");
    ASSERT_TRUE(file.is_open());
    const std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const size_t start = source.find("static void start_normal_runtime(");
    ASSERT_NE(start, std::string::npos);
    const size_t load = source.find("pad_config_init();", start);
    ASSERT_NE(load, std::string::npos);
    const size_t pads = source.find("list_source_pads_init()", start);
    const size_t notes = source.find("list_source_voice_notes_init()", start);
    ASSERT_NE(pads, std::string::npos);
    ASSERT_NE(notes, std::string::npos);
    EXPECT_LT(pads, load);
    EXPECT_LT(notes, load);
}

TEST_F(ListSourceTest, LvglPrefersPsramAndFallsBackForAllocationAndReallocation) {
    void* original = lv_malloc_core(16);
    ASSERT_NE(original, nullptr);
    EXPECT_EQ(non_psram_allocations, 0u);
    fail_allocations = true;
    void* resized = lv_realloc_core(original, 32);
    ASSERT_NE(resized, nullptr);
    void* fallback = lv_malloc_core(16);
    ASSERT_NE(fallback, nullptr);
    EXPECT_EQ(non_psram_allocations, 2u);
    non_psram_allocations = 0;
    fail_allocations = false;
    lv_free_core(fallback);
    lv_free_core(resized);
}

TEST_F(ListSourceTest, RegistryAcceptsExactTypedSourcesAndDocumentsFields) {
    static const ListSourceField fields[] = {
        {"id", "Stable ID", ListSourceFieldType::String, false},
        {"title", "Display title", ListSourceFieldType::String, true},
    };
    static const ListSourceDescriptor source = {"[notes:test]", "Recent notes", fields, 2};
    static const ListSourceDescriptor malformed = {"[notes:test|fallback]", "Bad", fields, 2};
    static const ListSourceDescriptor nested = {"[notes:[var:name]]", "Bad", fields, 2};
    static const ListSourceField missing_id_fields[] = {
        {"title", "Display title", ListSourceFieldType::String, true},
    };
    static const ListSourceDescriptor missing_id = {"[voice_note:other]", "Bad", missing_id_fields, 1};
    EXPECT_FALSE(list_source_register(&malformed));
    EXPECT_FALSE(list_source_register(&nested));
    EXPECT_FALSE(list_source_register(&missing_id));
    ASSERT_TRUE(list_source_register(&source));
    EXPECT_FALSE(list_source_register(&source));
    EXPECT_EQ(list_source_find("[notes:test]"), &source);
    EXPECT_EQ(list_source_find("[notes:test|fallback]"), nullptr);
    EXPECT_EQ(list_source_find("prefix [notes:test]"), nullptr);
    EXPECT_EQ(list_source_field(&source, "title"), &fields[1]);
    EXPECT_EQ(list_source_field(&source, "missing"), nullptr);
    EXPECT_EQ(list_source_at(0), &source);
    EXPECT_EQ(list_source_count(), 1);
    JsonDocument metadata;
    list_source_emit_metadata(metadata.to<JsonArray>());
    EXPECT_STREQ(metadata[0]["token"], "[notes:test]");
    EXPECT_STREQ(metadata[0]["fields"][1]["name"], "title");
    EXPECT_STREQ(metadata[0]["fields"][1]["type"], "string");
}

namespace {
uint32_t notes_revision = 1;
unsigned enriched_notes = 0;
bool recording = false;
bool invalid_note_title = false;
}

uint32_t voice_notes_catalog_revision() { return notes_revision; }
void voice_notes_snapshot(VoiceNoteSnapshot* output) {
    *output = {};
    output->session_active = recording;
}
VoiceNoteRecentResult voice_notes_select_recent(JsonDocument& output, size_t limit) {
    JsonArray entries = output.to<JsonArray>();
    for (size_t i = 0; i < limit; ++i) {
        JsonObject entry = entries.add<JsonObject>();
        char id[16];
        snprintf(id, sizeof(id), "note_%u", static_cast<unsigned>(i));
        entry["id"] = id;
        entry["duration_ms"] = 100;
    }
    return output.overflowed() ? VoiceNoteRecentResult::Error : VoiceNoteRecentResult::Ready;
}
VoiceNoteRecentResult voice_notes_enrich_recent(JsonObject entry) {
    ++enriched_notes;
    const std::string title = invalid_note_title ? "\xc3\x28" : std::string(126, 'x') + "\xc3\xa9";
    entry["title"] = title;
    entry["has_transcript"] = true;
    entry["has_enrichment"] = true;
    return VoiceNoteRecentResult::Ready;
}

TEST_F(ListSourceTest, RecentNotesLoadOnlyOnDemandAndEnrichOnePerStep) {
    list_source_voice_notes_init();
    const auto* source = list_source_find("[voice_note:recent_notes]");
    ASSERT_NE(source, nullptr);
    enriched_notes = 0;
    invalid_note_title = true;
    recording = false;
    list_source_loop();
    EXPECT_EQ(enriched_notes, 0u);
    auto* handle = list_source_acquire(source);
    ASSERT_NE(handle, nullptr);
    recording = true;
    list_source_loop();
    EXPECT_EQ(enriched_notes, 0u);
    recording = false;
    list_source_loop();
    EXPECT_EQ(enriched_notes, 0u);
    for (size_t i = 0; i < LIST_SOURCE_MAX_ITEMS; ++i) {
        list_source_loop();
        EXPECT_EQ(enriched_notes, i + 1);
        invalid_note_title = false;
    }
    ListSourceSnapshot* snapshot = nullptr;
    ASSERT_EQ(list_source_read(handle, &snapshot, nullptr, nullptr), ListSourceState::Ready);
    ASSERT_NE(snapshot, nullptr);
    ASSERT_EQ(snapshot->items().size(), LIST_SOURCE_MAX_ITEMS);
    EXPECT_TRUE(snapshot->items()[0]["title"].isNull());
    EXPECT_EQ(strlen(snapshot->items()[1]["title"]), 126u);
    EXPECT_TRUE(snapshot->items()[0]["has_transcript"].as<bool>());
    EXPECT_TRUE(snapshot->items()[0]["created_at"].isNull());
    snapshot->release();
    list_source_release(handle);
    list_source_loop();
}

#if TEST_LIST_SOURCE_HEALTH
namespace {
uint32_t health_now = 10000;
unsigned health_reads = 0;
int health_cpu = 52;
int16_t health_rssi = -67;
bool health_rssi_valid = true;
DeviceMemorySnapshot health_memory{};
WifiNetworkSnapshot health_network{};
}
extern "C" unsigned long millis() { return health_now; }
int device_telemetry_get_cpu_usage() { ++health_reads; return health_cpu; }
int16_t device_telemetry_get_cached_rssi(bool* valid) { *valid = health_rssi_valid; return health_rssi; }
DeviceMemorySnapshot device_telemetry_get_memory_snapshot() { return health_memory; }
void wifi_manager_get_network_snapshot(WifiNetworkSnapshot* out) { *out = health_network; }
uint8_t display_manager_get_backlight_brightness() { return 80; }
uint8_t audio_get_volume() { return 50; }

TEST_F(ListSourceTest, HealthPublishesRawValuesOnDemandAndSharesTwoSecondRefreshes) {
    if (!list_source_find("[health:metrics]")) {
        ASSERT_TRUE(list_source_health_init());
    }
    const auto* source = list_source_find("[health:metrics]");
    ASSERT_NE(source, nullptr);
    health_now = 10000;
    health_reads = 0;
    health_cpu = 52;
    health_rssi = -67;
    health_rssi_valid = true;
    health_memory.heap_internal_free_bytes = 201 * 1024;
    health_memory.psram_free_bytes = 512 * 1024;
    health_network.state = WifiNetworkState::Connected;
    strlcpy(health_network.ip, "192.168.1.42", sizeof(health_network.ip));
    list_source_loop();
    EXPECT_EQ(health_reads, 0u);
    auto* handle = list_source_acquire(source);
    auto* second = list_source_acquire(source);
    ASSERT_EQ(handle, second);
    list_source_loop();
    EXPECT_EQ(health_reads, 1u);
    ListSourceSnapshot* snapshot = nullptr;
    ASSERT_EQ(list_source_read(handle, &snapshot, nullptr, nullptr), ListSourceState::Ready);
    ASSERT_NE(snapshot, nullptr);
    EXPECT_EQ(snapshot->items().size(), 10u);
    EXPECT_TRUE(snapshot->items()[0]["value"].is<int>());
    EXPECT_EQ(snapshot->items()[0]["value"], 52);
    EXPECT_STREQ(snapshot->items()[0]["unit"], "%");
    EXPECT_STREQ(snapshot->items()[0]["severity"], "normal");
    EXPECT_EQ(snapshot->items()[1]["value"], 201 * 1024);
    EXPECT_STREQ(snapshot->items()[1]["severity"], "normal");
    EXPECT_EQ(snapshot->items()[2]["value"], 512 * 1024);
    EXPECT_STREQ(snapshot->items()[2]["severity"], "normal");
    EXPECT_STREQ(snapshot->items()[3]["severity"], "normal");
    EXPECT_EQ(snapshot->items()[4]["value"], 10);
    EXPECT_TRUE(snapshot->items()[4]["severity"].isNull());
    EXPECT_STREQ(snapshot->items()[5]["value"], "192.168.1.42");
    EXPECT_EQ(snapshot->items()[7]["value"], 50);
    EXPECT_STREQ(snapshot->items()[8]["value"], "ESP32-P4");
    snapshot->release();
    health_now = 11999;
    list_source_loop();
    EXPECT_EQ(health_reads, 1u);
    health_now = 12000;
    health_cpu = 60;
    health_memory.heap_internal_free_bytes = 200 * 1024;
    health_memory.psram_free_bytes = 20 * 1024;
    health_rssi = -75;
    list_source_loop();
    ASSERT_EQ(list_source_read(handle, &snapshot, nullptr, nullptr), ListSourceState::Ready);
    EXPECT_EQ(health_reads, 2u);
    EXPECT_STREQ(snapshot->items()[0]["severity"], "warning");
    EXPECT_STREQ(snapshot->items()[1]["severity"], "warning");
    EXPECT_STREQ(snapshot->items()[2]["severity"], "critical");
    EXPECT_STREQ(snapshot->items()[3]["severity"], "warning");
    snapshot->release();
    health_now = 14000;
    health_cpu = 85;
    health_memory.heap_internal_free_bytes = 100 * 1024;
    health_memory.psram_free_bytes = 200 * 1024;
    health_rssi = -76;
    list_source_loop();
    ASSERT_EQ(list_source_read(handle, &snapshot, nullptr, nullptr), ListSourceState::Ready);
    EXPECT_EQ(health_reads, 3u);
    EXPECT_STREQ(snapshot->items()[0]["severity"], "critical");
    EXPECT_STREQ(snapshot->items()[1]["severity"], "critical");
    EXPECT_STREQ(snapshot->items()[2]["severity"], "warning");
    EXPECT_STREQ(snapshot->items()[3]["severity"], "critical");
    snapshot->release();
    JsonDocument config;
    config["widget_data_binding"] = source->token;
    config["widget_list_id"] = "health";
    config["widget_list_label_top"] = "[item:title]";
    config["widget_list_label_bottom"] = "[item:value|Unavailable] [item:unit]";
    config["widget_list_style_bottom"] =
        "color:[expr:[item:severity|unknown]==\"critical\"?\"#dc2626\":\"#808080\"]";
    EXPECT_EQ(list_validate_config(config.as<JsonObjectConst>()), nullptr);
    list_source_release(second);
    list_source_release(handle);
    list_source_loop();
    health_now = 16000;
    list_source_loop();
    EXPECT_EQ(health_reads, 3u);
}

TEST_F(ListSourceTest, HealthUnavailableValuesAndAllocationFailuresAreExplicit) {
    if (!list_source_find("[health:metrics]")) {
        ASSERT_TRUE(list_source_health_init());
    }
    const auto* source = list_source_find("[health:metrics]");
    ASSERT_NE(source, nullptr);
    health_cpu = -1;
    health_network.state = WifiNetworkState::Off;
    auto* handle = list_source_acquire(source);
    ASSERT_NE(handle, nullptr);
    list_source_loop();
    ListSourceSnapshot* snapshot = nullptr;
    ASSERT_EQ(list_source_read(handle, &snapshot, nullptr, nullptr), ListSourceState::Ready);
    EXPECT_TRUE(snapshot->items()[0]["value"].isNull());
    EXPECT_STREQ(snapshot->items()[0]["severity"], "unknown");
    EXPECT_TRUE(snapshot->items()[3]["value"].isNull());
    EXPECT_STREQ(snapshot->items()[3]["severity"], "unknown");
    EXPECT_TRUE(snapshot->items()[5]["value"].isNull());
    snapshot->release();
    fail_allocations = true;
    health_now += 2000;
    list_source_loop();
    const char* error = nullptr;
    EXPECT_EQ(list_source_read(handle, &snapshot, nullptr, &error), ListSourceState::Error);
    EXPECT_EQ(snapshot, nullptr);
    EXPECT_NE(error, nullptr);
    fail_allocations = false;
    list_source_release(handle);
    list_source_loop();
}
#endif

TEST_F(ListSourceTest, ItemValidationFollowsPadAliasesAndEnforcesContext) {
    static const ListSourceField fields[] = {
        {"id", "Stable ID", ListSourceFieldType::String, false},
        {"title", "Display title", ListSourceFieldType::String, true},
    };
    static const ListSourceDescriptor source = {"[notes:recent]", "Notes", fields, 2};
    PadBinding bindings[] = {{"selected", "[item:id]"}, {"cycle", "[pad:cycle]"}};
    EXPECT_EQ(item_binding_validate("[expr:[item:title|none]==\"note\"?1:0]", &source, bindings, 2), nullptr);
    EXPECT_EQ(item_binding_validate("[pad:selected;%s]", &source, bindings, 2), nullptr);
    EXPECT_STREQ(item_binding_validate("[pad:selected]", nullptr, bindings, 2),
                 "item binding is unavailable in this context");
    EXPECT_EQ(item_binding_validate("[expr:[item:unknown|0]==0]", &source, bindings, 2), nullptr);
    EXPECT_STREQ(item_binding_validate("[expr:[item:unknown|0]==0]", nullptr, bindings, 2),
                 "item binding is unavailable in this context");
    EXPECT_STREQ(item_binding_validate("[pad:cycle]", &source, bindings, 2),
                 "recursive pad binding");
    EXPECT_STREQ(item_binding_validate("[list:notes.selected]", &source, bindings, 2),
                 "legacy list binding is not supported");

    std::string too_deep = "[item:id]";
    for (unsigned i = 0; i < 8; ++i) too_deep = "[expr:" + too_deep + "]";
    EXPECT_STREQ(item_binding_validate(too_deep.c_str(), &source, bindings, 2),
                 "binding exceeds token scan limits");
}

namespace {
void parse_normalized_list_action(const JsonObject&, ButtonAction& action) {
    strlcpy(action.payload.screen.screen_id, "normalized", sizeof(action.payload.screen.screen_id));
}

void parse_list_test_action(const JsonObject& json, ButtonAction& action) {
    strlcpy(action.payload.screen.screen_id, json["target"] | "", sizeof(action.payload.screen.screen_id));
}
bool list_test_fields(ButtonAction& action, ActionFieldVisitor visitor, void* context) {
    return action_visit_field(visitor, context, "target", action.payload.screen.screen_id);
}
const ActionTypeDef list_test_sync = {
    "list_test_sync", parse_list_test_action, nullptr, nullptr, list_test_fields, nullptr,
    nullptr, nullptr, ACTION_EXECUTION_SYNC
};
const ActionTypeDef list_test_pause = {
    "list_test_pause", parse_list_test_action, nullptr, nullptr, list_test_fields, nullptr,
    nullptr, nullptr, ACTION_EXECUTION_PAUSABLE
};
const ActionTypeDef list_test_late = {
    "list_test_late", parse_list_test_action, nullptr, nullptr, list_test_fields, nullptr,
    nullptr, nullptr, ACTION_EXECUTION_PAUSABLE, true, false
};

const char* validate_list_pad(JsonObjectConst pad, JsonObjectConst template_pad) {
    PadComposedContext context;
    const char* error = pad_compose_context(pad, template_pad, context);
    return error ? error : list_validate_pad_context(pad, context);
}
}

TEST_F(ListSourceTest, ActionValidationUsesDeclaredKeysEvenWhenParsingNormalizesText) {
    static const ActionTypeDef normalized = {
        "list_normalized", parse_normalized_list_action, nullptr, nullptr, list_test_fields, nullptr,
        nullptr, nullptr, ACTION_EXECUTION_SYNC
    };
    action_type_register(&normalized);
    static const ListSourceField fields[] = {{"id", "ID", ListSourceFieldType::String, false}};
    static const ListSourceDescriptor source{"[test:normalized]", "Test", fields, 1};
    JsonDocument input;
    JsonObject action = input.to<JsonArray>().add<JsonObject>();
    action["type"] = normalized.type_name;
    action["target"] = "[item:id]";
    EXPECT_EQ(item_binding_validate_actions(input.as<JsonArrayConst>(), &source, nullptr, 0), nullptr);
    EXPECT_NE(item_binding_validate_actions(input.as<JsonArrayConst>(), nullptr, nullptr, 0), nullptr);
    action["structural"] = "[item:id]";
    EXPECT_STREQ(item_binding_validate_actions(input.as<JsonArrayConst>(), &source, nullptr, 0),
                 "action 1 (list_normalized), field 'structural': this field does not support item bindings");
    EXPECT_STREQ(action["target"], "[item:id]");
}

TEST_F(ListSourceTest, ListConfigChecksSourceRowsGeometryAndOwnership) {
    static const ListSourceField fields[] = {
        {"id", "Stable ID", ListSourceFieldType::String, false},
        {"title", "Title", ListSourceFieldType::String, true},
    };
    static const ListSourceDescriptor source = {"[test:config]", "Test", fields, 2};
    ASSERT_TRUE(list_source_register(&source));
    JsonDocument doc;
    doc["widget_data_binding"] = source.token;
    doc["widget_list_id"] = "notes_1";
    EXPECT_EQ(list_validate_config(doc.as<JsonObjectConst>()), nullptr);
    uint8_t defaults[WIDGET_CONFIG_MAX_BYTES] = {};
    ASSERT_TRUE(list_parse_config(doc.as<JsonObject>(), defaults));
    ASSERT_NE(list_config(defaults), nullptr);
    EXPECT_STREQ(list_config(defaults)->label_top, "[item:title|Untitled]");
    EXPECT_TRUE(list_config(defaults)->allow_selection);
    EXPECT_TRUE(list_config(defaults)->show_navigation);
    list_release_config(defaults);
    doc["widget_list_allow_selection"] = false;
    doc["widget_list_show_navigation"] = false;
    doc["widget_list_label_top"] = "[item:title|Untitled]";
    doc["widget_list_style_top"] = "font_size:20;mode:dot;color:[item:id]";
    doc["widget_list_indicator_track_color"] = "[var:background]";
    doc["widget_list_indicator_color"] = "[var:foreground]";
    PadBinding aliases[] = {{"name", "[item:title]"}};
    EXPECT_EQ(list_validate_config(doc.as<JsonObjectConst>(), aliases, 1), nullptr);
    uint8_t data[WIDGET_CONFIG_MAX_BYTES] = {};
    fail_allocations = true;
    EXPECT_FALSE(list_parse_config(doc.as<JsonObject>(), data));
    EXPECT_EQ(list_config(data), nullptr);
    fail_allocations = false;
    ASSERT_TRUE(list_parse_config(doc.as<JsonObject>(), data));
    uint8_t copy[WIDGET_CONFIG_MAX_BYTES];
    memcpy(copy, data, sizeof(copy));
    list_retain_config(copy);
    list_release_config(data);
    const ListConfig* config = list_config(copy);
    ASSERT_NE(config, nullptr);
    EXPECT_EQ(config->source, &source);
    EXPECT_EQ(config->visible_items, 0);
    EXPECT_FALSE(config->allow_selection);
    EXPECT_FALSE(config->show_navigation);
    EXPECT_STREQ(config->label_top, "[item:title|Untitled]");
    EXPECT_STREQ(config->indicator_track_color, "[var:background]");
    EXPECT_STREQ(config->indicator_color, "[var:foreground]");
    unsigned visited = 0;
    list_visit_resources(copy, [](WidgetResourceKind kind, const char*, uint8_t,
                                  const char*, void* context) {
        if (kind == WidgetResourceKind::Binding) ++*static_cast<unsigned*>(context);
    }, &visited);
    EXPECT_EQ(visited, 16u);
    list_release_config(copy);
    EXPECT_EQ(list_config(copy), nullptr);

    doc["widget_list_show_navigation"] = "true";
    EXPECT_NE(list_validate_config(doc.as<JsonObjectConst>()), nullptr);
    doc["widget_list_show_navigation"] = true;
    doc["widget_list_viewport"] = "paged";
    for (unsigned slots : {1U, 2U}) {
        doc["widget_list_visible_items"] = slots;
        EXPECT_NE(list_validate_config(doc.as<JsonObjectConst>()), nullptr);
    }
    doc["widget_list_show_navigation"] = false;
    EXPECT_EQ(list_validate_config(doc.as<JsonObjectConst>()), nullptr);
    doc["widget_list_show_navigation"] = true;
    doc["widget_list_viewport"] = "scroll";
    EXPECT_EQ(list_validate_config(doc.as<JsonObjectConst>()), nullptr);
    doc["widget_list_visible_items"] = 3;
    doc["widget_list_viewport"] = "paged";
    EXPECT_EQ(list_validate_config(doc.as<JsonObjectConst>()), nullptr);
    doc["widget_list_visible_items"] = 0;
    EXPECT_EQ(list_validate_config(doc.as<JsonObjectConst>()), nullptr);

    doc["widget_list_allow_selection"] = "false";
    EXPECT_NE(list_validate_config(doc.as<JsonObjectConst>()), nullptr);
    doc["widget_list_allow_selection"] = 0;
    EXPECT_NE(list_validate_config(doc.as<JsonObjectConst>()), nullptr);
    doc["widget_list_allow_selection"] = false;
    EXPECT_EQ(list_validate_config(doc.as<JsonObjectConst>()), nullptr);

    doc["widget_list_label_top"] = "[pad:name]";
    EXPECT_EQ(list_validate_config(doc.as<JsonObjectConst>(), aliases, 1), nullptr);
    doc["widget_list_label_top"] = "[item:missing|none]";
    EXPECT_EQ(list_validate_config(doc.as<JsonObjectConst>(), aliases, 1), nullptr);
    doc["widget_list_label_top"] = "[item:title]";
    doc["widget_list_style_top"] = "color:[expr:[item:id]==\"x\"?\"#fff\":\"#000\"];mode:wrap";
    EXPECT_NE(list_validate_config(doc.as<JsonObjectConst>(), aliases, 1), nullptr);
    doc["widget_list_style_top"] = "font_size:[item:id]";
    EXPECT_NE(list_validate_config(doc.as<JsonObjectConst>(), aliases, 1), nullptr);
    doc["widget_list_style_top"] = "";
    doc["widget_list_padding"] = 28;
    EXPECT_EQ(list_validate_config(doc.as<JsonObjectConst>(), aliases, 1), nullptr);
    doc["widget_list_padding"] = 6;
    doc["widget_list_visible_items"] = "[item:id]";
    EXPECT_NE(list_validate_config(doc.as<JsonObjectConst>(), aliases, 1), nullptr);
    doc["widget_list_visible_items"] = 0;
    EXPECT_EQ(list_validate_config(doc.as<JsonObjectConst>(), aliases, 1), nullptr);
    doc["widget_list_visible_items"] = -1;
    EXPECT_NE(list_validate_config(doc.as<JsonObjectConst>(), aliases, 1), nullptr);
    doc["widget_list_visible_items"] = LIST_SOURCE_MAX_ITEMS + 1;
    EXPECT_NE(list_validate_config(doc.as<JsonObjectConst>(), aliases, 1), nullptr);
    doc["widget_list_visible_items"] = 3;
    doc["widget_list_indicator_color"] = "[item:title]";
    EXPECT_STREQ(list_validate_config(doc.as<JsonObjectConst>(), aliases, 1),
                 "List 'notes_1', field 'widget_list_indicator_color': position indicator colors have no item context; use a scalar binding instead");
    doc["widget_list_indicator_color"] = "[pad:name]";
    EXPECT_STREQ(list_validate_config(doc.as<JsonObjectConst>(), aliases, 1),
                 "List 'notes_1', field 'widget_list_indicator_color': position indicator colors have no item context; use a scalar binding instead");
    doc["widget_list_indicator_color"] = "#000000";
    doc["widget_list_indicator_track_color"] = "[var:background]";
    EXPECT_EQ(list_validate_config(doc.as<JsonObjectConst>(), aliases, 1), nullptr);
    doc.remove("widget_list_indicator_color");
    doc.remove("widget_list_indicator_track_color");
    doc["widget_list_selected_top_color"] = "[item:missing]";
    EXPECT_EQ(list_validate_config(doc.as<JsonObjectConst>(), aliases, 1), nullptr);
    doc["widget_list_selected_top_color"] = "[item:title]";
    doc["widget_list_selected_border_width"] = 0;
    EXPECT_EQ(list_validate_config(doc.as<JsonObjectConst>(), aliases, 1), nullptr);
    doc["widget_list_selected_border_width"] = 33;
    EXPECT_NE(list_validate_config(doc.as<JsonObjectConst>(), aliases, 1), nullptr);
    doc.remove("widget_list_selected_border_width");
    doc.remove("widget_list_selected_top_color");
    doc["widget_data_binding"] = "prefix [test:config]";
    EXPECT_NE(list_validate_config(doc.as<JsonObjectConst>(), aliases, 1), nullptr);
    doc["widget_data_binding"] = source.token;
    doc["widget_data_binding_2"] = "legacy";
    EXPECT_NE(list_validate_config(doc.as<JsonObjectConst>(), aliases, 1), nullptr);
    doc.remove("widget_data_binding_2");
    doc["widget_list_id"] = "not valid";
    EXPECT_NE(list_validate_config(doc.as<JsonObjectConst>(), aliases, 1), nullptr);
    doc["widget_list_id"] = "notes_1";
    doc["widget_list_viewport"] = "animated";
    EXPECT_NE(list_validate_config(doc.as<JsonObjectConst>(), aliases, 1), nullptr);
    doc["widget_list_viewport"] = "paged";
    doc["widget_list_bg_color"] = std::string(192, 'x');
    EXPECT_NE(list_validate_config(doc.as<JsonObjectConst>(), aliases, 1), nullptr);
    doc.remove("widget_list_bg_color");
    action_type_register(&list_test_sync);
    action_type_register(&list_test_pause);
    action_type_register(&list_test_late);
    JsonArray tap = doc["actions"].to<JsonArray>();
    tap.add<JsonObject>()["type"] = "list_test_pause";
    JsonObject next = tap.add<JsonObject>();
    next["type"] = "list_test_sync";
    next["target"] = "[pad:name]";
    EXPECT_STREQ(list_validate_actions(doc.as<JsonObjectConst>(), aliases, 1),
                 "List 'notes_1', field 'actions': action 2 (list_test_sync), field 'target': item context ended at action 1 (pause, confirmation, or navigation); capture the item in a variable first");
    next["target"] = "static";
    JsonArray hold = doc["lp_actions"].to<JsonArray>();
    JsonObject hold_boundary = hold.add<JsonObject>();
    hold_boundary["type"] = "list_test_pause";
    hold_boundary["target"] = "[item:id]";
    hold_boundary["unused"] = "~0~";
    EXPECT_EQ(list_validate_actions(doc.as<JsonObjectConst>(), aliases, 1), nullptr);
    hold_boundary["type"] = "list_test_late";
    EXPECT_STREQ(list_validate_actions(doc.as<JsonObjectConst>(), aliases, 1),
                 "List 'notes_1', field 'lp_actions': action 1 (list_test_late), field 'target': this action ends item context before resolving bindings; capture the item in a variable first");
    hold_boundary["type"] = "list_test_pause";
    JsonObject hold_later = hold.add<JsonObject>();
    hold_later["type"] = "list_test_sync";
    hold_later["target"] = "[pad:name]";
    EXPECT_NE(list_validate_actions(doc.as<JsonObjectConst>(), aliases, 1), nullptr);
    hold_later["target"] = "[item:missing]";
    EXPECT_NE(list_validate_actions(doc.as<JsonObjectConst>(), aliases, 1), nullptr);
}

TEST_F(ListSourceTest, ComposedPadValidatesListIdentityAndInheritedItemAliases) {
    static const ListSourceField fields[] = {
        {"id", "Stable ID", ListSourceFieldType::String, false},
        {"title", "Title", ListSourceFieldType::String, true},
    };
    static const ListSourceDescriptor source = {"[test:composed]", "Test", fields, 2};
    ASSERT_TRUE(list_source_register(&source));
    if (!action_type_find("list_test_sync")) action_type_register(&list_test_sync);
    if (!action_type_find("list_test_pause")) action_type_register(&list_test_pause);

    JsonDocument pad, inherited;
    pad["cols"] = 2;
    pad["rows"] = 1;
    pad["bindings"]["selected"] = "[item:id]";
    JsonObject list = pad["buttons"].to<JsonArray>().add<JsonObject>();
    list["col"] = 0;
    list["row"] = 0;
    list["widget_type"] = "list";
    list["widget_data_binding"] = source.token;
    list["widget_list_id"] = "notes";
    list["widget_list_label_top"] = "[pad:selected]";
    inherited["bindings"]["selected"] = "[item:missing]";
    JsonObject other = inherited["buttons"].to<JsonArray>().add<JsonObject>();
    other.set(list);
    other["col"] = 1;
    other["widget_list_id"] = "other";
    EXPECT_EQ(validate_list_pad(pad.as<JsonObjectConst>(), inherited.as<JsonObjectConst>()), nullptr);
    JsonObject control = pad["overlay_buttons"].to<JsonArray>().add<JsonObject>();
    JsonObject navigation = control["actions"].to<JsonArray>().add<JsonObject>();
    navigation["type"] = "list_nav";
    navigation["command"] = "next_page";
    navigation["target"] = "other";
    EXPECT_EQ(validate_list_pad(pad.as<JsonObjectConst>(), inherited.as<JsonObjectConst>()), nullptr);
    navigation["target"] = "missing";
    EXPECT_NE(validate_list_pad(pad.as<JsonObjectConst>(), inherited.as<JsonObjectConst>()), nullptr);
    navigation.clear();
    navigation["op"] = "list.navigate";
    navigation["args"]["command"] = "next_page";
    navigation["args"]["target"] = "other";
    EXPECT_EQ(validate_list_pad(pad.as<JsonObjectConst>(), inherited.as<JsonObjectConst>()), nullptr);
    navigation["args"]["target"] = "missing";
    EXPECT_NE(validate_list_pad(pad.as<JsonObjectConst>(), inherited.as<JsonObjectConst>()), nullptr);
    navigation["args"]["target"].to<JsonObject>()["$template"] = "[var:list]";
    EXPECT_EQ(validate_list_pad(pad.as<JsonObjectConst>(), inherited.as<JsonObjectConst>()), nullptr);
    navigation.clear();
    navigation["type"] = "list_nav";
    navigation["command"] = "next_page";
    navigation["target"] = "other";
    other["col"] = 0;
    EXPECT_NE(validate_list_pad(pad.as<JsonObjectConst>(), inherited.as<JsonObjectConst>()), nullptr);
    other["col"] = 1;
    pad.remove("overlay_buttons");

    other["widget_list_id"] = "notes";
    EXPECT_NE(validate_list_pad(pad.as<JsonObjectConst>(), inherited.as<JsonObjectConst>()), nullptr);
    other["col"] = 0;
    EXPECT_EQ(validate_list_pad(pad.as<JsonObjectConst>(), inherited.as<JsonObjectConst>()), nullptr);
    other["col"] = 1;
    other["widget_list_id"] = "other";

    JsonArray hold = list["lp_actions"].to<JsonArray>();
    hold.add<JsonObject>()["type"] = "list_test_pause";
    JsonObject later = hold.add<JsonObject>();
    later["type"] = "list_test_sync";
    later["target"] = "[pad:selected]";
    EXPECT_NE(validate_list_pad(pad.as<JsonObjectConst>(), inherited.as<JsonObjectConst>()), nullptr);
    later["target"] = "static";
    list["label_top"] = "[pad:selected]";
    EXPECT_NE(validate_list_pad(pad.as<JsonObjectConst>(), inherited.as<JsonObjectConst>()), nullptr);
}

TEST_F(ListSourceTest, PadCompositionPreservesLocalPrecedenceAndOverlayIndependence) {
    JsonDocument pad, inherited;
    pad["cols"] = 3;
    pad["rows"] = 1;
    pad["bindings"]["title"] = "local";
    inherited["bindings"]["title"] = "inherited";
    inherited["bindings"]["extra"] = "available";
    JsonObject local = pad["buttons"].to<JsonArray>().add<JsonObject>();
    local["col"] = 0;
    local["col_span"] = 2;
    local["label"] = "local";
    JsonObject overlay = pad["overlay_buttons"].to<JsonArray>().add<JsonObject>();
    overlay["col"] = 2;
    overlay["label"] = "overlay";
    JsonArray templates = inherited["buttons"].to<JsonArray>();
    for (unsigned col = 0; col < 4; ++col) {
        JsonObject button = templates.add<JsonObject>();
        button["col"] = col;
        button["label"] = "inherited";
    }
    PadComposedContext context;
    ASSERT_EQ(pad_compose_context(pad.as<JsonObjectConst>(), inherited.as<JsonObjectConst>(), context), nullptr);
    ASSERT_EQ(context.button_count, 3u);
    EXPECT_STREQ(context.buttons[0]["label"], "local");
    EXPECT_EQ(context.buttons[1]["col"].as<int>(), 2);
    EXPECT_STREQ(context.buttons[1]["label"], "inherited");
    EXPECT_STREQ(context.buttons[2]["label"], "overlay");
    ASSERT_EQ(context.binding_count, 2u);
    EXPECT_STREQ(context.bindings[0].value, "local");
    EXPECT_STREQ(context.bindings[1].value, "available");

    JsonArray overlays = pad["overlay_buttons"];
    while (overlays.size() + 1 < MAX_PAD_BUTTONS) overlays.add<JsonObject>();
    EXPECT_STREQ(pad_compose_context(pad.as<JsonObjectConst>(), inherited.as<JsonObjectConst>(), context),
                 "template buttons exceed capacity");
}

TEST_F(ListSourceTest, ItemValidationUsesIndependentContexts) {
    static const ListSourceField with_title[] = {
        {"id", "Stable ID", ListSourceFieldType::String, false},
        {"title", "Display title", ListSourceFieldType::String, true},
    };
    static const ListSourceDescriptor notes = {"[notes:recent]", "Notes", with_title, 2};
    std::atomic<bool> valid{true};
    auto verify = [&valid](const ListSourceDescriptor* source, bool should_pass) {
        for (unsigned i = 0; i < 1000; ++i) {
            const bool passed = item_binding_validate("[item:title]", source, nullptr, 0) == nullptr;
            if (passed != should_pass) valid.store(false);
        }
    };
    std::thread first(verify, &notes, true);
    std::thread second(verify, nullptr, false);
    first.join();
    second.join();
    EXPECT_TRUE(valid.load());
}

TEST_F(ListSourceTest, SnapshotOwnsDataAndCanBeShared) {
    auto* snapshot = ListSourceSnapshot::create();
    ASSERT_NE(snapshot, nullptr);
    JsonDocument input;
    input["id"] = "note-1";
    input["title"] = "Original";
    input["duration_ms"] = 1234;
    input["has_transcript"] = false;
    ASSERT_EQ(snapshot->append(input.as<JsonObjectConst>()), nullptr);
    EXPECT_TRUE(snapshot->items().isNull());
    ASSERT_EQ(snapshot->seal(), nullptr);
    input["title"] = "Changed";
    snapshot->retain();
    snapshot->release();
    JsonObjectConst item = snapshot->items()[0];
    EXPECT_STREQ(item["title"], "Original");
    EXPECT_EQ(item["duration_ms"].as<int>(), 1234);
    EXPECT_FALSE(item["has_transcript"].as<bool>());
    EXPECT_NE(snapshot->append(input.as<JsonObjectConst>()), nullptr);
    snapshot->release();
}

TEST_F(ListSourceTest, SupportsTwentyEightFieldItemsWithinBudget) {
    auto* snapshot = ListSourceSnapshot::create();
    ASSERT_NE(snapshot, nullptr);
    for (unsigned i = 0; i < 20; ++i) {
        JsonDocument input;
        std::string id = std::string("note-") + std::to_string(i);
        id.resize(63, 'i');
        input["id"] = id;
        for (unsigned field = 0; field < 7; ++field) {
            std::string key = std::string("field_") + std::to_string(field);
            key.resize(32, 'k');
            std::string text = std::to_string(i) + "-" + std::to_string(field);
            text.resize(127, 'x');
            input[key] = text;
        }
        ASSERT_EQ(snapshot->append(input.as<JsonObjectConst>()), nullptr);
    }
    ASSERT_EQ(snapshot->seal(), nullptr);
    EXPECT_EQ(snapshot->items().size(), 20u);
    EXPECT_LE(snapshot->peakBytes(), LIST_SOURCE_DOCUMENT_BYTES);
    snapshot->release();
}

TEST_F(ListSourceTest, DuplicateIdentityFailsWholeSnapshot) {
    auto* snapshot = ListSourceSnapshot::create();
    ASSERT_NE(snapshot, nullptr);
    JsonDocument input;
    input["id"] = "same";
    ASSERT_EQ(snapshot->append(input.as<JsonObjectConst>()), nullptr);
    EXPECT_STREQ(snapshot->append(input.as<JsonObjectConst>()), "duplicate item id");
    EXPECT_STREQ(snapshot->seal(), "duplicate item id");
    EXPECT_TRUE(snapshot->items().isNull());
    snapshot->release();
}

TEST_F(ListSourceTest, MoreThanTwentyItemsCannotBePublished) {
    auto* snapshot = ListSourceSnapshot::create();
    ASSERT_NE(snapshot, nullptr);
    JsonDocument input;
    for (unsigned i = 0; i < 21; ++i) {
        input["id"] = std::string("note-") + std::to_string(i);
        const char* error = snapshot->append(input.as<JsonObjectConst>());
        if (i < 20) ASSERT_EQ(error, nullptr);
        else EXPECT_STREQ(error, "too many items");
    }
    EXPECT_STREQ(snapshot->seal(), "too many items");
    snapshot->release();
}

TEST_F(ListSourceTest, RejectsInvalidIdentityAndNestedValues) {
    JsonDocument input;
    input["id"] = "bad\nid";
    auto* snapshot = ListSourceSnapshot::create();
    ASSERT_NE(snapshot, nullptr);
    EXPECT_STREQ(snapshot->append(input.as<JsonObjectConst>()), "invalid item id");
    EXPECT_NE(snapshot->seal(), nullptr);
    snapshot->release();

    snapshot = ListSourceSnapshot::create();
    ASSERT_NE(snapshot, nullptr);
    input["id"] = "good";
    input["nested"]["value"] = 1;
    EXPECT_STREQ(snapshot->append(input.as<JsonObjectConst>()), "item fields must be finite scalars");
    snapshot->release();
}

TEST_F(ListSourceTest, RejectsEmbeddedNulAndExcessFields) {
    JsonDocument input;
    const char invalid_id[] = {'n', '\0', 'x'};
    input["id"] = JsonString(invalid_id, sizeof(invalid_id), JsonString::Copied);
    auto* snapshot = ListSourceSnapshot::create();
    ASSERT_NE(snapshot, nullptr);
    EXPECT_STREQ(snapshot->append(input.as<JsonObjectConst>()), "invalid item id");
    snapshot->release();

    snapshot = ListSourceSnapshot::create();
    ASSERT_NE(snapshot, nullptr);
    input["id"] = "valid";
    for (unsigned i = 0; i < 8; ++i) {
        std::string key = std::string("field") + std::to_string(i);
        input[key] = i;
    }
    EXPECT_STREQ(snapshot->append(input.as<JsonObjectConst>()), "invalid item field count");
    snapshot->release();
}

TEST_F(ListSourceTest, FailsWithoutInternalFallback) {
    fail_allocations = true;
    EXPECT_EQ(ListSourceSnapshot::create(), nullptr);
    fail_allocations = false;
    auto* snapshot = ListSourceSnapshot::create();
    ASSERT_NE(snapshot, nullptr);
    JsonDocument input;
    input["id"] = "note";
    fail_allocations = true;
    EXPECT_STREQ(snapshot->append(input.as<JsonObjectConst>()), "snapshot allocation failed");
    EXPECT_NE(snapshot->seal(), nullptr);
    fail_allocations = false;
    snapshot->release();
}

TEST_F(ListSourceTest, ItemContextIsScopedAndDoesNotInterpretFieldContents) {
    auto* snapshot = ListSourceSnapshot::create();
    ASSERT_NE(snapshot, nullptr);
    JsonDocument input;
    input["id"] = "first";
    input["title"] = "[item:id]";
    input["created_at"] = 1704110400;
    input["duration_ms"] = 65000;
    ASSERT_EQ(snapshot->append(input.as<JsonObjectConst>()), nullptr);
    input["id"] = "second";
    input["title"] = "Other";
    ASSERT_EQ(snapshot->append(input.as<JsonObjectConst>()), nullptr);
    ASSERT_EQ(snapshot->seal(), nullptr);
    char out[192];
    binding_template_resolve("[item:id|none]", out, sizeof(out));
    EXPECT_STREQ(out, "none");
    {
        ItemBindingScope first(snapshot, 0);
        snapshot->release();
        binding_template_resolve("[item:title]", out, sizeof(out));
        EXPECT_STREQ(out, "[item:id]");
        binding_template_resolve("[expr:datetime([item:created_at],\"%H:%M\",\"UTC\")]", out, sizeof(out));
        EXPECT_STREQ(out, "12:00");
        binding_template_resolve("[expr:duration([item:duration_ms],\"ms\")]", out, sizeof(out));
        EXPECT_STREQ(out, "1:05");
        binding_template_resolve("[expr:datetime([item:missing])|unavailable]", out, sizeof(out));
        EXPECT_STREQ(out, "unavailable");
        std::thread other_task([] { ItemBindingScope::end_current(); });
        other_task.join();
        binding_template_resolve("[item:id]", out, sizeof(out));
        EXPECT_STREQ(out, "first");
        {
            ItemBindingScope second(snapshot, 1);
            binding_template_resolve("[item:id]", out, sizeof(out));
            EXPECT_STREQ(out, "second");
            binding_template_resolve("[expr:[item:id]==\"second\"?\"yes\":\"no\"]", out, sizeof(out));
            EXPECT_STREQ(out, "yes");
            {
                ItemBindingScope missing(nullptr, 0);
                binding_template_resolve("[item:id|none]", out, sizeof(out));
                EXPECT_STREQ(out, "none");
            }
        }
        binding_template_resolve("[item:id]", out, sizeof(out));
        EXPECT_STREQ(out, "first");
        binding_template_resolve("[item:missing|none]", out, sizeof(out));
        EXPECT_STREQ(out, "none");
        binding_template_resolve("[item:missing]", out, sizeof(out));
        EXPECT_STREQ(out, "---");
    }
    binding_template_resolve("[item:id|none]", out, sizeof(out));
    EXPECT_STREQ(out, "none");
}

namespace {
            uint32_t service_revision = 1;
            unsigned service_steps = 0;
            bool service_fail = false;
            bool service_defer = false;
            uint32_t service_version() { return service_revision; }

            ListSourceLoadStep service_step(void** job, ListSourceSnapshot* snapshot, const char** error) {
                ++service_steps;
                if (service_defer) return ListSourceLoadStep::Deferred;
                if (service_fail) {
                    *error = "test source failure";
                    return ListSourceLoadStep::Failed;
                }
                if (!*job) {
                    *job = heap_caps_malloc(1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                    if (!*job) {
                        *error = "test job allocation failed";
                        return ListSourceLoadStep::Failed;
                    }
                    return ListSourceLoadStep::Pending;
                }
                JsonDocument input;
                char id[16];
                snprintf(id, sizeof(id), "%u", static_cast<unsigned>(service_revision));
                input["id"] = id;
                *error = snapshot->append(input.as<JsonObjectConst>());
                return *error ? ListSourceLoadStep::Failed : ListSourceLoadStep::Complete;
            }

            void service_finish(void* job) { heap_caps_free(job); }
            }

            TEST_F(ListSourceTest, ServiceSharesDemandDiscardsStaleResultsAndBoundsRetiredSnapshots) {
                static const ListSourceField fields[] = {{"id", "Stable ID", ListSourceFieldType::String, false}};
                static const ListSourceDescriptor source = {"[test:service]", "Service", fields, 1,
                                                            service_version, service_step, service_finish};
                ASSERT_TRUE(list_source_register(&source));
                service_revision = 1;
                service_steps = 0;
                service_fail = false;
                service_defer = false;
                ota_active = false;
                list_source_loop();
                EXPECT_EQ(service_steps, 0u);
                auto* first = list_source_acquire(&source);
                ASSERT_NE(first, nullptr);
                auto* second = list_source_acquire(&source);
                ASSERT_EQ(second, first);
                ListSourceSnapshot* old = nullptr;
                EXPECT_EQ(list_source_read(first, &old, nullptr, nullptr), ListSourceState::Loading);
                ota_active = true;
                list_source_loop();
                EXPECT_EQ(service_steps, 0u);
                EXPECT_EQ(binding_refreshes, 0u);
                ota_active = false;
                list_source_loop();
                EXPECT_EQ(binding_refreshes, 0u);
                list_source_loop();
                EXPECT_EQ(binding_refreshes, 1u);
                ASSERT_EQ(list_source_read(first, &old, nullptr, nullptr), ListSourceState::Ready);
                ASSERT_NE(old, nullptr);
                EXPECT_STREQ(old->items()[0]["id"], "1");
                ListSourceSnapshot* shared = nullptr;
                ASSERT_EQ(list_source_read(second, &shared, nullptr, nullptr), ListSourceState::Ready);
                EXPECT_EQ(old, shared);
                shared->release();

                service_revision = 2;
                list_source_loop();
                service_revision = 3;
                list_source_loop();
                list_source_loop();
                ListSourceSnapshot* current = nullptr;
                ASSERT_EQ(list_source_read(first, &current, nullptr, nullptr), ListSourceState::Ready);
                EXPECT_STREQ(current->items()[0]["id"], "3");
                current->release();
                const unsigned before_blocked_refresh = service_steps;
                service_revision = 4;
                list_source_loop();
                EXPECT_EQ(service_steps, before_blocked_refresh);
                old->release();
                list_source_loop();
                list_source_loop();
                ASSERT_EQ(list_source_read(first, &current, nullptr, nullptr), ListSourceState::Ready);
                EXPECT_STREQ(current->items()[0]["id"], "4");
                current->release();
                EXPECT_EQ(binding_refreshes, 1u);

                service_revision = 5;
                service_fail = true;
                list_source_loop();
                const char* error = nullptr;
                EXPECT_EQ(list_source_read(first, &current, nullptr, &error), ListSourceState::Error);
                EXPECT_EQ(current, nullptr);
                EXPECT_STREQ(error, "test source failure");
                EXPECT_EQ(binding_refreshes, 1u);
                const unsigned after_failure = service_steps;
                list_source_loop();
                EXPECT_EQ(service_steps, after_failure);
                service_fail = false;
                service_defer = true;
                service_revision = 6;
                list_source_loop();
                EXPECT_EQ(list_source_read(first, nullptr, nullptr, nullptr), ListSourceState::Loading);
                service_defer = false;
                list_source_loop();
                list_source_release(first);
                list_source_release(second);
                list_source_loop();
                EXPECT_EQ(binding_refreshes, 1u);

                first = list_source_acquire(&source);
                ASSERT_NE(first, nullptr);
                service_fail = true;
                list_source_loop();
                EXPECT_EQ(list_source_read(first, nullptr, nullptr, &error), ListSourceState::Error);
                EXPECT_EQ(binding_refreshes, 2u);
                list_source_loop();
                EXPECT_EQ(binding_refreshes, 2u);
                service_fail = false;
                list_source_release(first);
                list_source_loop();
            }

#if defined(TEST_LIST_SOURCE_PADS)
#include "list_source_pads.h"
#include <LittleFS.h>

FakeIndexedStorage LittleFS;

namespace {
std::atomic<uint32_t> pads_revision{1};
std::string pad_names[MAX_PADS];
bool pad_present[MAX_PADS] = {};
bool pad_read_error = false;
unsigned pad_reads = 0;
unsigned pad_releases = 0;
PadConfig cached_pad = {};
const ListSourceDescriptor* pads_source() {
    static const bool registered = list_source_pads_init();
    return registered ? list_source_find("[pad:pads]") : nullptr;
}
}

uint32_t pad_config_get_generation() { return pads_revision.load(); }
bool pad_config_lock_mutations() { return !pad_read_error; }
void pad_config_unlock_mutations() {}
void pad_config_path(uint8_t page, char* out, size_t size) {
    snprintf(out, size, "/config/pad_%u.json", page);
}
const PadConfig* pad_config_acquire(uint8_t page) {
    ++pad_reads;
    if (!pad_present[page]) return nullptr;
    cached_pad.name = pad_names[page].c_str();
    return &cached_pad;
}
void pad_config_release(const PadConfig*) { ++pad_releases; }

TEST_F(ListSourceTest, GateOneSampleUsesValidPadListContext) {
    ASSERT_NE(pads_source(), nullptr);
    std::ifstream file("docs/samples/action-framework-gate1.json");
    ASSERT_TRUE(file.is_open());
    std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    JsonDocument document;
    ASSERT_FALSE(deserializeJson(document, source));
    JsonObjectConst list = document["buttons"][2];
    EXPECT_EQ(list_validate_config(list), nullptr);
    EXPECT_EQ(list_validate_actions(list, nullptr, 0), nullptr);
    EXPECT_EQ(validate_list_pad(document.as<JsonObjectConst>(), JsonObjectConst()), nullptr);
}

TEST_F(ListSourceTest, PadsLoadLazilyOneAtATimeAndRefreshByRevision) {
    auto* source = pads_source();
    ASSERT_NE(source, nullptr);
    EXPECT_NE(list_source_field(source, "id"), nullptr);
    EXPECT_NE(list_source_field(source, "title"), nullptr);
    EXPECT_EQ(list_source_field(source, "label"), nullptr);
    pad_reads = 0;
    pad_present[1] = true;
    pad_names[1] = "Kitchen";
    pad_present[3] = true;
    pad_names[3] = "";
    list_source_loop();
    EXPECT_EQ(pad_reads, 0u);
    auto* handle = list_source_acquire(source);
    ASSERT_NE(handle, nullptr);
    for (unsigned i = 0; i < MAX_PADS; ++i) {
        const unsigned before = pad_reads;
        list_source_loop();
        EXPECT_EQ(pad_reads, before + 1);
    }
    ListSourceSnapshot* snapshot = nullptr;
    ASSERT_EQ(list_source_read(handle, &snapshot, nullptr, nullptr), ListSourceState::Ready);
    ASSERT_EQ(snapshot->items().size(), 2u);
    EXPECT_STREQ(snapshot->items()[0]["id"], "pad_1");
    EXPECT_STREQ(snapshot->items()[0]["title"], "Pad 1: Kitchen");
    EXPECT_STREQ(snapshot->items()[1]["id"], "pad_3");
    EXPECT_STREQ(snapshot->items()[1]["title"], "Pad 3");
    snapshot->release();

    pad_names[1] = "Old";
    ++pads_revision;
    list_source_loop();
    list_source_loop();
    pad_names[1] = "New";
    ++pads_revision;
    for (unsigned i = 0; i < MAX_PADS; ++i) list_source_loop();
    ASSERT_EQ(list_source_read(handle, &snapshot, nullptr, nullptr), ListSourceState::Ready);
    EXPECT_STREQ(snapshot->items()[0]["title"], "Pad 1: New");
    snapshot->release();

    pad_read_error = true;
    ++pads_revision;
    list_source_loop();
    const char* error = nullptr;
    EXPECT_EQ(list_source_read(handle, &snapshot, nullptr, &error), ListSourceState::Error);
    EXPECT_STREQ(error, "pad configuration unavailable");
    EXPECT_EQ(snapshot, nullptr);
    pad_read_error = false;
    list_source_release(handle);
    list_source_loop();
}

TEST_F(ListSourceTest, PadsBoundNamesAndReportAllocationFailure) {
    const auto* source = pads_source();
    ASSERT_NE(source, nullptr);
    pad_present[1] = true;
    pad_names[1] = std::string(117, 'x') + "\xF0\x9F\x98\x80";
    pad_present[3] = true;
    pad_names[3] = "\xC0\x80";
    ++pads_revision;
    auto* handle = list_source_acquire(source);
    ASSERT_NE(handle, nullptr);
    for (unsigned i = 0; i < MAX_PADS; ++i) list_source_loop();
    ListSourceSnapshot* snapshot = nullptr;
    ASSERT_EQ(list_source_read(handle, &snapshot, nullptr, nullptr), ListSourceState::Ready);
    JsonArrayConst items = snapshot->items();
    ASSERT_EQ(items.size(), 2u);
    EXPECT_EQ(strlen(items[0]["title"].as<const char*>()), 124u);
    EXPECT_STREQ(items[1]["title"], "Pad 3");
    snapshot->release();

    void* job = nullptr;
    snapshot = ListSourceSnapshot::create();
    ASSERT_NE(snapshot, nullptr);
    const char* error = nullptr;
    fail_allocations = true;
    EXPECT_EQ(source->step(&job, snapshot, &error), ListSourceLoadStep::Failed);
    EXPECT_STREQ(error, "pad source allocation failed");
    fail_allocations = false;
    source->finish(job);
    snapshot->release();

    snapshot = ListSourceSnapshot::create();
    ASSERT_NE(snapshot, nullptr);
    job = heap_caps_malloc(sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ASSERT_NE(job, nullptr);
    *static_cast<uint16_t*>(job) = 1;
    fail_allocations = true;
    EXPECT_EQ(source->step(&job, snapshot, &error), ListSourceLoadStep::Failed);
    EXPECT_STREQ(error, "pad metadata allocation failed");
    fail_allocations = false;
    source->finish(job);
    snapshot->release();

    ++pads_revision;
    fail_allocations = true;
    list_source_loop();
    fail_allocations = false;
    error = nullptr;
    EXPECT_EQ(list_source_read(handle, nullptr, nullptr, &error), ListSourceState::Error);
    EXPECT_STREQ(error, "snapshot allocation failed");
    list_source_release(handle);
    list_source_loop();
    pad_present[1] = pad_present[3] = false;
}

TEST_F(ListSourceTest, PadsReadCachedOrFilteredMetadataAndPropagateFailures) {
    char name[128];
    const char* error = nullptr;
    LittleFS = FakeIndexedStorage();
    pad_present[2] = true;
    pad_names[2] = "Cached";
    const unsigned releases = pad_releases;
    EXPECT_EQ(pad_config_list_read(2, name, sizeof(name), &error), PadListReadResult::Present);
    EXPECT_STREQ(name, "Cached");
    EXPECT_EQ(pad_releases, releases + 1);
    EXPECT_EQ(LittleFS.opens.size(), 0u);
    pad_names[2] = std::string(126, 'x') + "\xF0\x9F\x98\x80";
    EXPECT_EQ(pad_config_list_read(2, name, sizeof(name), &error), PadListReadResult::Present);
    EXPECT_EQ(strlen(name), 126u);

    pad_present[2] = false;
    EXPECT_EQ(pad_config_list_read(2, name, sizeof(name), &error), PadListReadResult::Missing);
    const std::string json = "{\"name\":\"From flash\",\"ignored\":[1,2,3]}";
    LittleFS.files["/config/pad_2.json"] = std::vector<uint8_t>(json.begin(), json.end());
    EXPECT_EQ(pad_config_list_read(2, name, sizeof(name), &error), PadListReadResult::Present);
    EXPECT_STREQ(name, "From flash");

    const std::string large_ignored = "{\"name\":\"Small\",\"ignored\":\"" +
                                      std::string(20000, 'x') + "\"}";
    LittleFS.files["/config/pad_2.json"] = std::vector<uint8_t>(large_ignored.begin(), large_ignored.end());
    EXPECT_EQ(pad_config_list_read(2, name, sizeof(name), &error), PadListReadResult::Present);
    EXPECT_STREQ(name, "Small");

    const std::string no_name = "{\"ignored\":1}";
    LittleFS.files["/config/pad_2.json"] = std::vector<uint8_t>(no_name.begin(), no_name.end());
    EXPECT_EQ(pad_config_list_read(2, name, sizeof(name), &error), PadListReadResult::Present);
    EXPECT_STREQ(name, "");
    LittleFS.fail_open_path = "/config/pad_2.json";
    EXPECT_EQ(pad_config_list_read(2, name, sizeof(name), &error), PadListReadResult::Error);
    EXPECT_STREQ(error, "pad metadata open failed");
    LittleFS.fail_open_path.clear();
    fail_allocations = true;
    EXPECT_EQ(pad_config_list_read(2, name, sizeof(name), &error), PadListReadResult::Error);
    EXPECT_STREQ(error, "pad metadata allocation failed");
    fail_allocations = false;
    LittleFS.short_read_path = "/config/pad_2.json";
    EXPECT_EQ(pad_config_list_read(2, name, sizeof(name), &error), PadListReadResult::Error);
    EXPECT_STREQ(error, "pad metadata parse failed");
    LittleFS.short_read_path.clear();
    LittleFS.files["/config/pad_2.json"] = {'{'};
    EXPECT_EQ(pad_config_list_read(2, name, sizeof(name), &error), PadListReadResult::Error);
    EXPECT_STREQ(error, "pad metadata parse failed");
    LittleFS = FakeIndexedStorage();
}
#endif
