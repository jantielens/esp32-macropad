#include "list_source.h"
#include "device_classes/shutter_tester/shutter_test_scripts.h"

#include <LittleFS.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

FakeIndexedStorage LittleFS;

namespace {
size_t live_allocations = 0;
size_t non_psram_allocations = 0;
bool fail_script_scratch = false;
}

void* heap_caps_malloc(size_t size, uint32_t caps) {
    if (!(caps & MALLOC_CAP_SPIRAM)) ++non_psram_allocations;
    if (fail_script_scratch && size == sizeof(ShutterTestScript)) return nullptr;
    void* memory = malloc(size);
    if (memory) ++live_allocations;
    return memory;
}

void* heap_caps_realloc(void* memory, size_t size, uint32_t caps) {
    if (!(caps & MALLOC_CAP_SPIRAM)) ++non_psram_allocations;
    void* next = realloc(memory, size);
    if (next && !memory) ++live_allocations;
    return next;
}

void heap_caps_free(void* memory) {
    if (memory) --live_allocations;
    free(memory);
}

void display_manager_lock_if_needed(bool* locked) { *locked = false; }
void display_manager_unlock_if_needed(bool) {}
bool ota_activity_is_active() { return false; }
bool shutter_measure_is_valid_speed(const char* speed) {
    return !strcmp(speed, "1/125") || !strcmp(speed, "1/60");
}

void list_source_shutter_tests_init();

class ShutterSourceTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { list_source_shutter_tests_init(); }

    void SetUp() override {
        LittleFS = FakeIndexedStorage();
        ASSERT_EQ(live_allocations, 0u);
        non_psram_allocations = 0;
        fail_script_scratch = false;
    }

    void TearDown() override {
        EXPECT_EQ(live_allocations, 0u);
        EXPECT_EQ(non_psram_allocations, 0u);
    }

    void set_file(const std::string& contents) {
        auto& bytes = LittleFS.files[SHUTTER_TEST_FILE_PATH];
        bytes.assign(contents.begin(), contents.end());
    }
};

TEST_F(ShutterSourceTest, BoundedParserRejectsOversizedIdsWithoutChangingLegacyParser) {
    auto result = std::make_unique<ShutterTestParseResult>();
    for (const char* suffix : {" | Display", ""}) {
        set_file(std::string("name: 1234567890123456") + suffix + "\n1/125\n");
        ASSERT_TRUE(shutter_test_scripts_parse_bounded(result.get()));
        ASSERT_EQ(result->count, 1);
        EXPECT_STREQ(result->scripts[0].id, "1234567890123456");

        set_file(std::string("name: valid\n1/60\nname: 12345678901234567") +
                 suffix + "\n1/125\n");
        EXPECT_FALSE(shutter_test_scripts_parse_bounded(result.get()));
        ASSERT_EQ(shutter_test_scripts_parse(result.get()), 2);
        EXPECT_STREQ(result->scripts[1].id, "1234567890123456");
    }
}

TEST_F(ShutterSourceTest, OversizedIdFailsTheWholeSnapshot) {
    const auto* source = list_source_find("[shutter:tests]");
    ASSERT_NE(source, nullptr);
    for (const char* suffix : {" | Display", ""}) {
        set_file(std::string("name: valid\n1/60\nname: 12345678901234567") +
                 suffix + "\n1/125\n");
        auto* handle = list_source_acquire(source);
        ASSERT_NE(handle, nullptr);
        list_source_loop();
        ListSourceSnapshot* snapshot = nullptr;
        EXPECT_EQ(list_source_read(handle, &snapshot, nullptr, nullptr), ListSourceState::Error);
        EXPECT_EQ(snapshot, nullptr);
        list_source_release(handle);
        list_source_loop();
    }
}

TEST_F(ShutterSourceTest, DuplicateAndExcessScriptsFailInsteadOfPublishingPartialResults) {
    auto result = std::make_unique<ShutterTestParseResult>();
    set_file("name: duplicate\n1/125\nname: duplicate\n1/60\n");
    EXPECT_FALSE(shutter_test_scripts_parse_bounded(result.get()));
    ASSERT_EQ(shutter_test_scripts_parse(result.get()), 1);
    EXPECT_STREQ(result->scripts[0].speeds[0].speed, "1/60");

    std::string contents;
    for (unsigned index = 0; index <= SHUTTER_TEST_MAX_SCRIPTS; ++index)
        contents += "name: test" + std::to_string(index) + "\n1/125\n";
    set_file(contents);
    EXPECT_FALSE(shutter_test_scripts_parse_bounded(result.get()));
    EXPECT_EQ(shutter_test_scripts_parse(result.get()), SHUTTER_TEST_MAX_SCRIPTS);
}

TEST_F(ShutterSourceTest, OverlongLinesFailRatherThanSplitIntoSeparateEntries) {
    auto result = std::make_unique<ShutterTestParseResult>();
    set_file("name: valid | " + std::string(300, 'x') + "\n1/125\n");
    EXPECT_FALSE(shutter_test_scripts_parse_bounded(result.get()));
}

TEST_F(ShutterSourceTest, ScratchAllocationFailureFailsClosed) {
    set_file("name: valid\n1/125\n");
    auto result = std::make_unique<ShutterTestParseResult>();
    fail_script_scratch = true;
    EXPECT_FALSE(shutter_test_scripts_parse_bounded(result.get()));
    EXPECT_EQ(live_allocations, 0u);

    const auto* source = list_source_find("[shutter:tests]");
    ASSERT_NE(source, nullptr);
    auto* handle = list_source_acquire(source);
    ASSERT_NE(handle, nullptr);
    list_source_loop();
    ListSourceSnapshot* snapshot = nullptr;
    EXPECT_EQ(list_source_read(handle, &snapshot, nullptr, nullptr), ListSourceState::Error);
    EXPECT_EQ(snapshot, nullptr);
    list_source_release(handle);
    list_source_loop();
    fail_script_scratch = false;
}

TEST_F(ShutterSourceTest, InvalidOptionalNamesAreOmittedWithoutLosingRows) {
    std::string content = "name: bad_utf8 | ";
    content.append("\xc3\x28", 2);
    content += "\n1/125\nname: valid | A name\nshots_per_speed: 3\n1/125\n1/60\n";
    content += "name: bad_nul | Bad";
    content.push_back('\0');
    content += "name\n1/125\nname: split_utf8 | ";
    content.append(62, 'x');
    content.append("\xc3\xa9", 2);
    content += "\n1/125\n";
    set_file(content);

    const auto* source = list_source_find("[shutter:tests]");
    ASSERT_NE(source, nullptr);
    const auto* title_field = list_source_field(source, "title");
    ASSERT_NE(title_field, nullptr);
    EXPECT_TRUE(title_field->optional);
    EXPECT_EQ(list_source_field(source, "name"), nullptr);
    auto* handle = list_source_acquire(source);
    ASSERT_NE(handle, nullptr);
    for (int i = 0; i < 4; ++i) list_source_loop();

    ListSourceSnapshot* snapshot = nullptr;
    ASSERT_EQ(list_source_read(handle, &snapshot, nullptr, nullptr), ListSourceState::Ready);
    ASSERT_NE(snapshot, nullptr);
    JsonArrayConst rows = snapshot->items();
    ASSERT_EQ(rows.size(), 4u);
    EXPECT_STREQ(rows[0]["id"], "bad_utf8");
    EXPECT_TRUE(rows[0]["title"].isNull());
    EXPECT_STREQ(rows[1]["id"], "valid");
    EXPECT_STREQ(rows[1]["title"], "A name");
    EXPECT_EQ(rows[1]["shots_per_speed"].as<int>(), 3);
    EXPECT_EQ(rows[1]["speed_count"].as<int>(), 2);
    EXPECT_STREQ(rows[2]["id"], "bad_nul");
    EXPECT_TRUE(rows[2]["title"].isNull());
    EXPECT_STREQ(rows[3]["id"], "split_utf8");
    EXPECT_EQ(strlen(rows[3]["title"].as<const char*>()), 62u);
    snapshot->release();
    list_source_release(handle);
    list_source_loop();
}
