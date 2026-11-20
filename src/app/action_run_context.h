#pragma once

#include "board_config.h"

#if HAS_DISPLAY || HAS_BUTTON

#include <stdint.h>

struct ActionEventData;
struct PadBinding;
class ListSourceSnapshot;
struct ActionRunContext;

#if HAS_MCP
#include "action_result.h"
struct HaServicePayload;
struct ActionRunObserver {
    void* data;
    bool (*dispatch_ha)(void*, uint8_t, const HaServicePayload&, ActionResult*);
    void (*failed)(void*, uint8_t);
    void (*release)(void*);
};
#endif

class ActionRunScope {
public:
    explicit ActionRunScope(const ActionRunContext* context);
    ~ActionRunScope();
    ActionRunScope(const ActionRunScope&) = delete;
    ActionRunScope& operator=(const ActionRunScope&) = delete;
private:
    const ActionRunContext* previous_;
};

const ActionRunContext* action_run_current();
ActionRunContext* action_run_capture(const PadBinding* origin_bindings = nullptr,
                                     uint8_t origin_binding_count = 0,
                                     bool use_active_bindings = true);
void action_run_retain(ActionRunContext* context);
void action_run_release(ActionRunContext* context);
const ActionEventData* action_run_event(const ActionRunContext* context);
#if HAS_MCP
void action_run_set_observer(ActionRunContext* context, const ActionRunObserver& observer);
void action_run_next_action(ActionRunContext* context);
void action_run_action_failed(ActionRunContext* context);
bool action_run_dispatch_ha(const HaServicePayload& payload, ActionResult* result);
#endif
#if HAS_DISPLAY
const PadBinding* action_run_bindings(const ActionRunContext* context, uint8_t* count);
#endif
#if HAS_DISPLAY && HAS_PSRAM
ListSourceSnapshot* action_run_item(const ActionRunContext* context);
#endif

#endif
