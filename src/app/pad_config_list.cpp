#include "pad_config.h"

#if HAS_DISPLAY && HAS_PSRAM

#include "bounded_psram_json_allocator.h"
#include "log_manager.h"
#include "storage.h"

#include <stdio.h>
#include <string.h>

static void copy_name(char* output, size_t capacity, const char* name) {
    size_t length = strlen(name);
    if (length >= capacity) {
        length = capacity - 1;
        while (length && (static_cast<unsigned char>(name[length]) & 0xc0) == 0x80) --length;
    }
    memcpy(output, name, length);
    output[length] = '\0';
}

PadListReadResult pad_config_list_read(uint8_t page, char* name, size_t name_size,
                                       const char** error) {
    if (error) *error = nullptr;
    if (!name || !name_size || page >= MAX_PADS) {
        if (error) *error = "invalid pad metadata request";
        return PadListReadResult::Error;
    }
    name[0] = '\0';
    const PadConfigMutationGuard guard;
    if (!guard) {
        if (error) *error = "pad configuration unavailable";
        return PadListReadResult::Error;
    }
    const PadConfig* config = pad_config_acquire(page);
    if (config) {
        if (config->name) copy_name(name, name_size, config->name);
        pad_config_release(config);
        return PadListReadResult::Present;
    }

    char path[32];
    pad_config_path(page, path, sizeof(path));
    if (!Storage.exists(path)) return PadListReadResult::Missing;
    File file = Storage.open(path, "r");
    if (!file) {
        if (error) *error = "pad metadata open failed";
        return PadListReadResult::Error;
    }
    const size_t size = file.size();
    if (!size || size > 64 * 1024) {
        file.close();
        if (error) *error = "invalid pad configuration size";
        return PadListReadResult::Error;
    }
    BoundedPsramJsonAllocator allocator(16 * 1024);
    JsonDocument filter(&allocator);
    filter["name"] = true;
    JsonDocument document(&allocator);
    DeserializationError result = filter.overflowed()
        ? DeserializationError::NoMemory
        : deserializeJson(document, file, DeserializationOption::Filter(filter));
    file.close();
    if (result || document.overflowed()) {
        if (error) *error = result == DeserializationError::NoMemory || document.overflowed()
            ? "pad metadata allocation failed" : "pad metadata parse failed";
        return PadListReadResult::Error;
    }
    if (!document.is<JsonObject>()) {
        if (error) *error = "pad metadata parse failed";
        return PadListReadResult::Error;
    }
    if (document["name"].is<const char*>()) {
        JsonString value = document["name"].as<JsonString>();
        if (strlen(value.c_str()) == value.size()) {
            copy_name(name, name_size, value.c_str());
        } else {
            LOGW("PadCfg", "Pad %u has an invalid name", page);
        }
    } else if (!document["name"].isNull()) {
        LOGW("PadCfg", "Pad %u has an invalid name", page);
    }
    return PadListReadResult::Present;
}

#endif
