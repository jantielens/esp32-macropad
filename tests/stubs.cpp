// ============================================================================
// Test stubs — libc functions missing on glibc (Linux)
// ============================================================================

#include <cstring>
#include <cstddef>
#include <cstdint>

#if ACTION_LIST_HOST_ISOLATED
#include "action_run_context.h"
#include "event_binding.h"
#if HAS_DISPLAY && HAS_PSRAM
#include "item_binding.h"
ItemBindingScope::ItemBindingScope(ListSourceSnapshot*, size_t)
    : snapshot_(nullptr), index_(0), previous_(nullptr) {}
ItemBindingScope::~ItemBindingScope() {}
#endif
ActionRunScope::ActionRunScope(const ActionRunContext*) : previous_(nullptr) {}
ActionRunScope::~ActionRunScope() {}
ActionRunContext* action_run_capture(const PadBinding*, uint8_t, bool) { return nullptr; }
void action_run_retain(ActionRunContext*) {}
void action_run_release(ActionRunContext*) {}
const ActionEventData* action_run_event(const ActionRunContext*) { return nullptr; }
#if HAS_DISPLAY && HAS_PSRAM
ListSourceSnapshot* action_run_item(const ActionRunContext*) { return nullptr; }
#endif
EventBindingScope::EventBindingScope(const ActionEventData*) : previous_(nullptr) {}
EventBindingScope::~EventBindingScope() {}
#endif

#if LIST_TEST_BUTTON_DEFAULTS
#include "button_defaults.h"
ButtonDefaults list_test_defaults{};
const ButtonDefaults* button_defaults_get() { return &list_test_defaults; }
#endif

#if ALARM_ENABLED
bool __attribute__((weak)) alarm_command_submit(const char*, uint8_t, int, uint8_t) { return false; }
void __attribute__((weak)) alarm_command_report_error(const char*) {}
#endif

// strlcpy is available on ESP32 (newlib) and BSD but not older glibc.
// glibc 2.38+ (Ubuntu 24.04) provides it natively — skip our fallback.
#if !defined(__GLIBC__) || (__GLIBC__ < 2) || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 38)
extern "C" size_t strlcpy(char* dst, const char* src, size_t siz) {
    size_t len = strlen(src);
    if (siz > 0) {
        size_t copy = len < siz - 1 ? len : siz - 1;
        memcpy(dst, src, copy);
        dst[copy] = '\0';
    }
    return len;
}
#endif

#if IS_SHUTTER_TESTER && !TEST_SHUTTER_OPERATIONS
// Weak stubs for shutter_session_actions hooks called from shutter_session.cpp.
// Real implementation lives in src/app/shutter_session_actions.cpp which has
// flash / FreeRTOS / LVGL dependencies that the host tests do not link.
void shutter_session_actions_dispatch_start() {}
void shutter_session_actions_notify_complete() {}
#endif
