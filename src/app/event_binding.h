#pragma once

#include "board_config.h"

#if HAS_DISPLAY || HAS_BUTTON

#include "binding_template.h"

struct ActionEventData {
    const char* value;
    const char* step;
    BindingResolvedValueKind value_kind;
};

class EventBindingScope {
public:
    explicit EventBindingScope(const ActionEventData* event);
    ~EventBindingScope();
    EventBindingScope(const EventBindingScope&) = delete;
    EventBindingScope& operator=(const EventBindingScope&) = delete;
private:
    const ActionEventData* previous_;
};

const ActionEventData* event_binding_current();
void event_binding_init();

#endif
