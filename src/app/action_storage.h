#ifndef ACTION_STORAGE_H
#define ACTION_STORAGE_H

#include "board_config.h"

#if HAS_DISPLAY || HAS_BUTTON

#include <stddef.h>
#include <stdint.h>

struct ActionOperationDef;
struct ButtonAction;

struct ActionStoredArgument {
    union {
        int32_t integer;
        float number;
    };
    uint16_t offset;
    bool dynamic;
};

struct ActionArgumentBlock {
    ActionArgumentBlock* next;
    uint16_t bytes;
    uint8_t count;
};

struct ActionStorage {
    ActionArgumentBlock* blocks;
    bool failed;
};

class ActionStorageScope {
public:
    explicit ActionStorageScope(ActionStorage* storage);
    ~ActionStorageScope();
    ActionStorageScope(const ActionStorageScope&) = delete;
    ActionStorageScope& operator=(const ActionStorageScope&) = delete;
private:
    ActionStorage* previous_;
};

ActionStorage* action_storage_current();
void action_storage_clear(ActionStorage& storage);
void action_storage_move(ActionStorage& destination, ActionStorage& source);
void* action_storage_allocate(size_t size);
void action_storage_free(void* ptr);
bool action_storage_copy_actions(ActionStorage& storage, ButtonAction* destination,
                                 const ButtonAction* source, uint8_t count);
ActionArgumentBlock* action_storage_copy(ActionStorage& storage, const ActionArgumentBlock* source);
const ActionStoredArgument* action_storage_arguments(const ActionArgumentBlock* block);
const char* action_storage_text(const ActionArgumentBlock* block, const ActionStoredArgument& argument);

#endif

#endif // ACTION_STORAGE_H
