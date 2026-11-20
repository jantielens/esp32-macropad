#include "list_source_pads.h"

#if HAS_DISPLAY && HAS_PSRAM

#include "list_source.h"
#include "log_manager.h"
#include "pad_config.h"

#include <esp_heap_caps.h>
#include <stdio.h>
#include <string.h>

namespace {

bool append_name(char* label, size_t capacity, const char* name) {
    size_t output = strlen(label);
    size_t length;
    if (!list_source_preview_length(name, strlen(name), capacity - output - 1, &length)) return false;
    memcpy(label + output, name, length);
    label[output + length] = '\0';
    return true;
}

ListSourceLoadStep pads_step(void** job, ListSourceSnapshot* snapshot, const char** error) {
    if (!*job) {
        *job = heap_caps_malloc(sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!*job) {
            *error = "pad source allocation failed";
            return ListSourceLoadStep::Failed;
        }
        *static_cast<uint16_t*>(*job) = 0;
    }
    auto& page = *static_cast<uint16_t*>(*job);
    if (page >= MAX_PADS) return ListSourceLoadStep::Complete;

    char name[LIST_SOURCE_MAX_TEXT_BYTES + 1];
    const uint16_t current = page++;
    const PadListReadResult result = pad_config_list_read(current, name, sizeof(name), error);
    if (result == PadListReadResult::Error) return ListSourceLoadStep::Failed;
    if (result == PadListReadResult::Present) {
        char id[LIST_SOURCE_MAX_ID_BYTES + 1];
        char title[LIST_SOURCE_MAX_TEXT_BYTES + 1];
        snprintf(id, sizeof(id), "pad_%u", static_cast<unsigned>(current));
        snprintf(title, sizeof(title), "Pad %u", static_cast<unsigned>(current));
        const size_t base_length = strlen(title);
        if (name[0]) {
            strncat(title, ": ", sizeof(title) - strlen(title) - 1);
            if (!append_name(title, sizeof(title), name)) {
                LOGW("ListSource", "Pad %u has an invalid name", static_cast<unsigned>(current));
                title[base_length] = '\0';
            } else if (strlen(title) == base_length + 2) {
                title[base_length] = '\0';
            }
        }
        BoundedPsramJsonAllocator allocator(8 * 1024);
        JsonDocument item(&allocator);
        item["id"] = id;
        item["title"] = title;
        if (item.overflowed()) {
            *error = "pad metadata allocation failed";
            return ListSourceLoadStep::Failed;
        }
        *error = snapshot->append(item.as<JsonObjectConst>());
        if (*error) return ListSourceLoadStep::Failed;
    }
    return page >= MAX_PADS ? ListSourceLoadStep::Complete : ListSourceLoadStep::Pending;
}

void pads_finish(void* job) { heap_caps_free(job); }

const ListSourceField fields[] = {
    {"id", "Stable pad ID", ListSourceFieldType::String, false},
    {"title", "Pad number and optional name", ListSourceFieldType::String, false},
};
const ListSourceDescriptor source = {"[pad:pads]", "Pads", fields, 2,
                                     pad_config_get_generation, pads_step, pads_finish};
}

bool list_source_pads_init() { return list_source_register(&source); }

#endif
