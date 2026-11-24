#include "action_storage.h"

#if HAS_DISPLAY || HAS_BUTTON

#include "action_registry.h"
#include <stdlib.h>
#include <string.h>
#if HAS_PSRAM
#include <esp_heap_caps.h>
#endif

namespace {
thread_local ActionStorage* current_storage = nullptr;

void* allocate(size_t size) {
#if HAS_PSRAM
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return malloc(size);
#endif
}

void release(void* ptr) {
#if HAS_PSRAM
    heap_caps_free(ptr);
#else
    free(ptr);
#endif
}

ActionArgumentBlock* make_block(ActionStorage& storage, uint8_t count, size_t text_bytes) {
    const size_t size = sizeof(ActionArgumentBlock) + count * sizeof(ActionStoredArgument) + text_bytes;
    if (size > UINT16_MAX) {
        storage.failed = true;
        return nullptr;
    }
    auto* block = static_cast<ActionArgumentBlock*>(allocate(size));
    if (!block) {
        storage.failed = true;
        return nullptr;
    }
    memset(block, 0, size);
    block->next = storage.blocks;
    block->bytes = static_cast<uint16_t>(size);
    block->count = count;
    storage.blocks = block;
    return block;
}
}

ActionStorageScope::ActionStorageScope(ActionStorage* storage) : previous_(current_storage) {
    current_storage = storage;
}

ActionStorageScope::~ActionStorageScope() {
    current_storage = previous_;
}

ActionStorage* action_storage_current() { return current_storage; }

void action_storage_clear(ActionStorage& storage) {
    while (storage.blocks) {
        ActionArgumentBlock* block = storage.blocks;
        storage.blocks = block->next;
        release(block);
    }
    storage.failed = false;
}

void action_storage_move(ActionStorage& destination, ActionStorage& source) {
    while (source.blocks) {
        ActionArgumentBlock* block = source.blocks;
        source.blocks = block->next;
        block->next = destination.blocks;
        destination.blocks = block;
    }
    destination.failed |= source.failed;
    source.failed = false;
}

void* action_storage_allocate(size_t size) { return allocate(size); }
void action_storage_free(void* ptr) { if (ptr) release(ptr); }

bool action_storage_copy_actions(ActionStorage& storage, ButtonAction* destination,
                                 const ButtonAction* source, uint8_t count) {
    if (count && (!destination || !source)) return false;
    if (count) memcpy(destination, source, count * sizeof(ButtonAction));
    for (uint8_t index = 0; index < count; ++index) {
        if (destination[index].type[0] != '@') continue;
        ActionArgumentBlock* copy = action_storage_copy(storage, source[index].payload.operation.arguments);
        if (!copy) return false;
        destination[index].payload.operation.arguments = copy;
    }
    return true;
}

const ActionStoredArgument* action_storage_arguments(const ActionArgumentBlock* block) {
    return reinterpret_cast<const ActionStoredArgument*>(block + 1);
}

const char* action_storage_text(const ActionArgumentBlock* block, const ActionStoredArgument& argument) {
    return reinterpret_cast<const char*>(block) + argument.offset;
}

ActionArgumentBlock* action_storage_parse(ActionStorage& storage,
                                         const ActionOperationDef& operation,
                                         JsonObjectConst arguments) {
    size_t text_bytes = 0;
    for (uint8_t index = 0; index < operation.argument_count; ++index) {
        const ActionArgumentDef& argument = operation.arguments[index];
        JsonVariantConst value = arguments[argument.name];
        if (value.is<JsonObjectConst>()) text_bytes += strlen(value["$template"].as<const char*>()) + 1;
        else if (action_argument_is_text(argument.type))
            text_bytes += strlen(arguments.containsKey(argument.name)
                ? value.as<const char*>() : argument.default_text) + 1;
    }
    ActionArgumentBlock* block = make_block(storage, operation.argument_count, text_bytes);
    if (!block) return nullptr;
    auto* stored = reinterpret_cast<ActionStoredArgument*>(block + 1);
    size_t offset = sizeof(ActionArgumentBlock) + operation.argument_count * sizeof(ActionStoredArgument);
    for (uint8_t index = 0; index < operation.argument_count; ++index) {
        const ActionArgumentDef& argument = operation.arguments[index];
        const bool present = arguments.containsKey(argument.name);
        JsonVariantConst value = arguments[argument.name];
        stored[index].dynamic = value.is<JsonObjectConst>();
        if (stored[index].dynamic || action_argument_is_text(operation.arguments[index].type)) {
            const char* text = stored[index].dynamic
                ? value["$template"].as<const char*>()
                : present ? value.as<const char*>() : argument.default_text;
            size_t length = strlen(text);
            stored[index].offset = static_cast<uint16_t>(offset);
            memcpy(reinterpret_cast<char*>(block) + offset, text, length + 1);
            offset += length + 1;
        } else if (operation.arguments[index].type == ACTION_ARGUMENT_NUMBER) {
            stored[index].number = value.as<float>();
        } else {
            stored[index].integer = !present ? argument.default_integer :
                operation.arguments[index].type == ACTION_ARGUMENT_BOOLEAN
                ? (value.as<bool>() ? 1 : 0) : value.as<int32_t>();
        }
    }
    return block;
}

ActionArgumentBlock* action_storage_copy(ActionStorage& storage, const ActionArgumentBlock* source) {
    if (!source || source->bytes < sizeof(ActionArgumentBlock) +
            source->count * sizeof(ActionStoredArgument)) {
        storage.failed = true;
        return nullptr;
    }
    auto* block = static_cast<ActionArgumentBlock*>(allocate(source->bytes));
    if (!block) {
        storage.failed = true;
        return nullptr;
    }
    memcpy(block, source, source->bytes);
    block->next = storage.blocks;
    storage.blocks = block;
    return block;
}

#endif
