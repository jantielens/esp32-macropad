#include "board_config.h"

#if IS_SHUTTER_TESTER && HAS_DISPLAY && HAS_PSRAM

#include "list_source.h"
#include "log_manager.h"
#include "shutter_test_scripts.h"

#include <esp_heap_caps.h>
#include <new>
#include <string.h>

namespace {
struct ShutterTestsJob {
    ShutterTestParseResult result;
    uint8_t index;
};

ListSourceLoadStep shutter_tests_step(void** opaque, ListSourceSnapshot* snapshot,
                                      const char** error) {
    if (shutter_test_scripts_write_active()) return ListSourceLoadStep::Deferred;
    auto* job = static_cast<ShutterTestsJob*>(*opaque);
    if (!job) {
        void* memory = heap_caps_malloc(sizeof(ShutterTestsJob), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!memory) {
            *error = "shutter test allocation failed";
            return ListSourceLoadStep::Failed;
        }
        job = new (memory) ShutterTestsJob{};
        *opaque = job;
        if (!shutter_test_scripts_parse_bounded(&job->result)) {
            *error = "cannot read shutter tests";
            return ListSourceLoadStep::Failed;
        }
        if (!job->result.count) return ListSourceLoadStep::Complete;
    }

    const ShutterTestScript& script = job->result.scripts[job->index];
    BoundedPsramJsonAllocator allocator(LIST_SOURCE_DOCUMENT_BYTES);
    JsonDocument item(&allocator);
    item["id"] = script.id;
    if (script.name[0]) {
        size_t length;
        if (list_source_preview_length(script.name, strlen(script.name),
                                       LIST_SOURCE_MAX_TEXT_BYTES, &length))
            item["title"] = JsonString(script.name, length, JsonString::Copied);
        else
            LOGW("ListSource", "Shutter test has an invalid name");
    }
    item["shots_per_speed"] = script.shots_per_speed;
    item["speed_count"] = script.speed_count;
    if (item.overflowed()) {
        *error = "shutter test metadata allocation failed";
        return ListSourceLoadStep::Failed;
    }
    if ((*error = snapshot->append(item.as<JsonObjectConst>()))) return ListSourceLoadStep::Failed;
    return ++job->index == job->result.count ? ListSourceLoadStep::Complete : ListSourceLoadStep::Pending;
}

void shutter_tests_finish(void* opaque) {
    auto* job = static_cast<ShutterTestsJob*>(opaque);
    if (!job) return;
    job->~ShutterTestsJob();
    heap_caps_free(job);
}

const ListSourceField shutter_test_fields[] = {
    {"id", "Guided test ID", ListSourceFieldType::String, false},
    {"title", "Guided test name when available", ListSourceFieldType::String, true},
    {"shots_per_speed", "Shots per speed", ListSourceFieldType::Number, false},
    {"speed_count", "Number of speeds", ListSourceFieldType::Number, false},
};
const ListSourceDescriptor shutter_tests_source{
    "[shutter:tests]", "Guided tests", shutter_test_fields,
    sizeof(shutter_test_fields) / sizeof(shutter_test_fields[0]),
    shutter_test_scripts_revision, shutter_tests_step, shutter_tests_finish
};
}

void list_source_shutter_tests_init() {
    if (!list_source_register(&shutter_tests_source))
        LOGE("ListSource", "Cannot register shutter tests");
}

#endif
