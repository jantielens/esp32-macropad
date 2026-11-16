#include <gtest/gtest.h>
#include <LittleFS.h>

#include "fs_indexed_store.h"
#include "voice_wav.h"
#include "voice_notes_metadata.h"
#include "bounded_psram_json_allocator.h"

FakeIndexedStorage LittleFS;

namespace {
bool fail_allocation = false;
bool have_psram = false;
unsigned psram_allocations = 0;
const char* const kFields[] = {"size_bytes", "duration_ms", "sample_rate", "channels"};
} // namespace

void* heap_caps_malloc(size_t size, uint32_t caps) {
    if (caps & MALLOC_CAP_SPIRAM) ++psram_allocations;
    return fail_allocation ? nullptr : malloc(size);
}
void heap_caps_free(void* ptr) { free(ptr); }
bool psramFound() { return have_psram; }

namespace {

void add_wav(const char* path, uint32_t data_bytes = 320, uint32_t rate = 16000,
             uint16_t channels = 1) {
    std::vector<uint8_t> bytes(VOICE_WAV_HEADER_BYTES + data_bytes, 0);
    voice_wav_build_header(bytes.data(), data_bytes, rate, channels, 16);
    LittleFS.files[path] = std::move(bytes);
}

class IndexedStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        LittleFS = FakeIndexedStorage();
        fail_allocation = false;
        have_psram = false;
        psram_allocations = 0;
    }
};

TEST_F(IndexedStoreTest, RegistersAndReloadsWavWithoutReadingPayloadForList) {
    FsIndexedStore store("/clips", kFields, 4, nullptr, 0, ".wav", voice_notes_metadata_read_metadata);
    ASSERT_TRUE(store.begin());
    ASSERT_EQ(store.count(), 0);
    add_wav("/clips/first.wav");
    StaticJsonDocument<128> metadata;
    metadata["duration_ms"] = 999;
    ASSERT_TRUE(store.register_pre_written("first", 0, metadata.as<JsonObject>()));
    ASSERT_FALSE(store.register_pre_written("first", 0, metadata.as<JsonObject>()));
    const unsigned reads = LittleFS.opens["/clips/first.wav"];

    FsIndexedStore reloaded("/clips", kFields, 4, nullptr, 0, ".wav", voice_notes_metadata_read_metadata);
    ASSERT_TRUE(reloaded.begin());
    DynamicJsonDocument manifest(4096);
    ASSERT_EQ(deserializeJson(manifest, reloaded.list()), DeserializationError::Ok);
    JsonObject entry = manifest["entries"][0];
    EXPECT_EQ(entry["id"].as<std::string>(), "first");
    EXPECT_FALSE(entry.containsKey("created_at"));
    EXPECT_EQ(entry["duration_ms"].as<uint32_t>(), 10u);
    EXPECT_EQ(entry["size_bytes"].as<uint32_t>(), 364u);
    EXPECT_EQ(entry["sample_rate"].as<uint32_t>(), 16000u);
    EXPECT_EQ(entry["channels"].as<uint16_t>(), 1);
    EXPECT_EQ(LittleFS.opens["/clips/first.wav"], reads);
    EXPECT_EQ(reloaded.count(), 1);
    EXPECT_TRUE(reloaded.remove("first"));
    EXPECT_FALSE(LittleFS.exists("/clips/first.wav"));
}

TEST_F(IndexedStoreTest, RebuildsOnlyValidCompletedWavs) {
    FsIndexedStore store("/clips", kFields, 4, nullptr, 0, ".wav", voice_notes_metadata_read_metadata);
    LittleFS.mkdir("/clips");
    add_wav("/clips/valid.wav", 400, 48000, 2);
    add_wav("/clips/named.tmp.wav");
    add_wav("/clips/pending.wav.tmp");
    add_wav("/clips/not-wav.wav.bak");
    add_wav("/clips/broken.wav");
    LittleFS.files["/clips/broken.wav"][0] = 'X';
    LittleFS.files["/clips/_index.json"] = {'{', 'b', 'a', 'd'};
    ASSERT_TRUE(store.begin());
    DynamicJsonDocument manifest(4096);
    ASSERT_EQ(deserializeJson(manifest, store.list()), DeserializationError::Ok);
    ASSERT_EQ(manifest["entries"].size(), 2u);
    EXPECT_TRUE(store.exists("valid"));
    EXPECT_TRUE(store.exists("named.tmp"));
    EXPECT_FALSE(store.exists("broken"));
    EXPECT_FALSE(store.exists("pending"));
}

TEST_F(IndexedStoreTest, CopiesCachedMetadataWithoutPayloadReadsAndReportsAllocationFailure) {
    FsIndexedStore store("/clips", kFields, 4, nullptr, 0, ".wav", voice_notes_metadata_read_metadata);
    ASSERT_TRUE(store.begin());
    add_wav("/clips/first.wav");
    StaticJsonDocument<128> metadata;
    ASSERT_TRUE(store.register_pre_written("first", 10, metadata.as<JsonObject>()));
    const unsigned reads = LittleFS.opens["/clips/first.wav"];

    JsonDocument manifest;
    ASSERT_TRUE(store.copy_manifest(manifest));
    ASSERT_EQ(manifest["entries"].size(), 1u);
    JsonDocument entry;
    ASSERT_TRUE(store.copy_entry("first", entry.to<JsonObject>()));
    EXPECT_EQ(entry["id"].as<std::string>(), "first");
    EXPECT_EQ(entry["created_at"].as<uint32_t>(), 10u);
    EXPECT_FALSE(entry.containsKey("entries"));
    EXPECT_FALSE(store.copy_entry("missing", entry.as<JsonObject>()));
    EXPECT_EQ(LittleFS.opens["/clips/first.wav"], reads);

    BoundedPsramJsonAllocator allocator(0);
    JsonDocument exhausted(&allocator);
    EXPECT_FALSE(store.copy_manifest(exhausted));
    EXPECT_TRUE(store.exists("first"));
    ASSERT_TRUE(store.remove("first"));
    EXPECT_EQ(entry["id"].as<std::string>(), "first");
    EXPECT_EQ(manifest["entries"][0]["id"].as<std::string>(), "first");
}

TEST_F(IndexedStoreTest, CopiesOnlyMostRecentEntriesWithStableTimestampTies) {
    FsIndexedStore store("/clips", kFields, 4, nullptr, 0, ".wav", voice_notes_metadata_read_metadata);
    ASSERT_TRUE(store.begin());
    StaticJsonDocument<128> metadata;
    for (const auto& entry : {std::pair<const char*, uint32_t>{"a", 0}, {"b", 0},
                              {"c", 10}, {"d", 20}, {"e", 20}}) {
        std::string path = std::string("/clips/") + entry.first + ".wav";
        add_wav(path.c_str());
        ASSERT_TRUE(store.register_pre_written(entry.first, entry.second, metadata.as<JsonObject>()));
    }
    const unsigned reads = LittleFS.opens["/clips/c.wav"];
    BoundedPsramJsonAllocator allocator(8192);
    JsonDocument selected(&allocator);
    ASSERT_TRUE(store.copy_recent(selected, 3));
    ASSERT_EQ(selected.size(), 3u);
    EXPECT_STREQ(selected[0]["id"], "e");
    EXPECT_STREQ(selected[1]["id"], "d");
    EXPECT_STREQ(selected[2]["id"], "c");
    EXPECT_EQ(LittleFS.opens["/clips/c.wav"], reads);
    ASSERT_TRUE(store.copy_recent(selected, 5));
    EXPECT_STREQ(selected[3]["id"], "b");
    EXPECT_STREQ(selected[4]["id"], "a");
    fail_allocation = true;
    EXPECT_FALSE(store.copy_recent(selected, 3));
    fail_allocation = false;
    EXPECT_FALSE(store.copy_recent(selected, FS_STORE_RECENT_MAX_ENTRIES + 1));
}

TEST_F(IndexedStoreTest, SortsAndRebuildsEntriesWithPsram) {
    have_psram = true;
    FsIndexedStore store("/clips", kFields, 4, nullptr, 0, ".wav", voice_notes_metadata_read_metadata);
    ASSERT_TRUE(store.begin());
    StaticJsonDocument<128> metadata;
    add_wav("/clips/older.wav");
    add_wav("/clips/newer.wav");
    ASSERT_TRUE(store.register_pre_written("older", 10, metadata.as<JsonObject>()));
    ASSERT_TRUE(store.register_pre_written("newer", 20, metadata.as<JsonObject>()));
    DynamicJsonDocument manifest(4096);
    ASSERT_EQ(deserializeJson(manifest, store.list()), DeserializationError::Ok);
    EXPECT_EQ(manifest["entries"][0]["id"].as<std::string>(), "newer");
    EXPECT_EQ(manifest["entries"][1]["id"].as<std::string>(), "older");
    EXPECT_GT(psram_allocations, 0u);

    ASSERT_TRUE(LittleFS.remove("/clips/_index.json"));
    FsIndexedStore reloaded("/clips", kFields, 4, nullptr, 0, ".wav", voice_notes_metadata_read_metadata);
    ASSERT_TRUE(reloaded.begin());
    EXPECT_EQ(reloaded.count(), 2);
    EXPECT_TRUE(reloaded.exists("older"));
    EXPECT_TRUE(reloaded.exists("newer"));
}

TEST_F(IndexedStoreTest, ReportsIoAndDeletionFailuresWithoutLosingEntry) {
    FsIndexedStore store("/clips", kFields, 4, nullptr, 0, ".wav", voice_notes_metadata_read_metadata);
    LittleFS.mkdir("/clips");
    add_wav("/clips/first.wav");
    LittleFS.short_read_path = "/clips/first.wav";
    EXPECT_FALSE(store.begin());
    EXPECT_TRUE(store.list().isEmpty());
    EXPECT_EQ(store.count(), -1);
    LittleFS.short_read_path.clear();
    ASSERT_TRUE(store.begin());
    ASSERT_EQ(store.count(), 1);
    LittleFS.fail_remove_path = "/clips/first.wav";
    EXPECT_FALSE(store.remove("first"));
    EXPECT_TRUE(store.exists("first"));
    EXPECT_TRUE(LittleFS.exists("/clips/first.wav"));
    LittleFS.fail_remove_path.clear();
    EXPECT_TRUE(store.remove("first"));
}

TEST_F(IndexedStoreTest, RejectsInvalidPayloadAndManifestWriteFailure) {
    FsIndexedStore store("/clips", kFields, 4, nullptr, 0, ".wav", voice_notes_metadata_read_metadata);
    ASSERT_TRUE(store.begin());
    ASSERT_EQ(store.count(), 0);
    StaticJsonDocument<128> metadata;
    add_wav("/clips/invalid.wav");
    LittleFS.files["/clips/invalid.wav"][0] = 'X';
    EXPECT_FALSE(store.register_pre_written("invalid", 0, metadata.as<JsonObject>()));
    EXPECT_EQ(store.count(), 0);
    ASSERT_TRUE(LittleFS.remove("/clips/invalid.wav"));

    add_wav("/clips/first.wav");
    LittleFS.fail_rename = true;
    EXPECT_FALSE(store.register_pre_written("first", 0, metadata.as<JsonObject>()));
    ASSERT_TRUE(LittleFS.remove("/clips/first.wav"));
    EXPECT_TRUE(store.list().isEmpty());
    LittleFS.fail_rename = false;
    EXPECT_EQ(store.count(), 0);
}

TEST_F(IndexedStoreTest, ReportsRebuildAllocationFailureAndCanRetry) {
    FsIndexedStore store("/clips", kFields, 4, nullptr, 0, ".wav", voice_notes_metadata_read_metadata);
    LittleFS.mkdir("/clips");
    add_wav("/clips/first.wav");
    fail_allocation = true;
    EXPECT_FALSE(store.begin());
    EXPECT_TRUE(store.list().isEmpty());
    EXPECT_EQ(store.count(), -1);
    EXPECT_FALSE(LittleFS.exists("/clips/_index.json"));
    fail_allocation = false;
    ASSERT_TRUE(store.begin());
    EXPECT_EQ(store.count(), 1);
}

TEST_F(IndexedStoreTest, JsonStoresRetainTimestampAndPayloadBehavior) {
    const char* const fields[] = {"duration_ms"};
    FsIndexedStore store("/sessions", fields, 1);
    ASSERT_TRUE(store.begin());
    StaticJsonDocument<128> metadata;
    metadata["duration_ms"] = 20;
    EXPECT_FALSE(store.register_pre_written("missing-time", 0, metadata.as<JsonObject>()));
    ASSERT_TRUE(store.add("one", "{\"created_at\":123,\"duration_ms\":20}", metadata.as<JsonObject>()));
    EXPECT_TRUE(store.exists("one"));
    EXPECT_STREQ(store.data_path("one").c_str(), "/sessions/one.json");
    metadata["duration_ms"] = 42;
    ASSERT_TRUE(store.patch_meta("one", metadata.as<JsonObject>()));
    DynamicJsonDocument payload(1024);
    ASSERT_EQ(deserializeJson(payload, store.get("one")), DeserializationError::Ok);
    EXPECT_EQ(payload["duration_ms"].as<int>(), 42);
    ASSERT_TRUE(LittleFS.remove("/sessions/_index.json"));
    FsIndexedStore reloaded("/sessions", fields, 1);
    ASSERT_TRUE(reloaded.begin());
    ASSERT_EQ(reloaded.count(), 1);
    DynamicJsonDocument manifest(4096);
    ASSERT_EQ(deserializeJson(manifest, reloaded.list()), DeserializationError::Ok);
    EXPECT_EQ(manifest["entries"][0]["duration_ms"].as<int>(), 42);
    EXPECT_TRUE(reloaded.remove("one"));
}
} // namespace
