#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <vector>

#include "action_registry.h"

static unsigned registry_errors = 0;
static char registry_error[256] = {};
void log_noop(const char* module, const char* format, ...) {
    if (strcmp(module, "Action")) return;
    ++registry_errors;
    va_list args;
    va_start(args, format);
    vsnprintf(registry_error, sizeof(registry_error), format, args);
    va_end(args);
}

static bool fake_enabled = true;
static bool fake_available() { return fake_enabled; }
static ActionResult fake_dispatch(const ActionResolvedArgument*, const char*, uint32_t) {
    return ACTION_COMPLETE;
}
static const ActionOperationDef fake_operations[] = {
    {"test.device.operation", "Device operation", nullptr, 0, fake_dispatch}
};
static const ActionTypeDef fake_action_type = {
    "fake", nullptr, fake_available,
    ACTION_EXECUTION_SYNC, true, false, fake_operations, 1
};

static int failures = 0;
static void check(bool result, const char* message) {
    if (!result) {
        std::printf("FAIL: %s\n", message);
        ++failures;
    }
}

static void test_operation_registry() {
    action_type_register(&fake_action_type);
    const ActionTypeDef* owner = nullptr;
    check(action_operation_find("test.device.operation", &owner) == &fake_operations[0],
          "registered operation is found");
    check(owner == &fake_action_type, "operation retains its owner");

    ButtonAction action = {};
    action.type[0] = '@';
    action.payload.operation.descriptor = &fake_operations[0];
    check(action_operation_for_action(action, &owner) == &fake_operations[0] &&
          owner == &fake_action_type, "stored operation resolves through registry");
    check(!action_operation_find("@test.device.operation"), "wire IDs do not accept discriminator");

    fake_enabled = false;
    check(!action_type_is_supported("fake"), "unavailable owner is excluded");
    check(!action_operation_find("test.device.operation", &owner) && !owner,
          "unavailable operation is excluded from lookup");
    check(!action_operation_for_action(action), "stored unavailable operation cannot dispatch");
    fake_enabled = true;
}

static void test_registry_overflow_reporting() {
    static char names[64][16] = {};
    std::vector<ActionTypeDef> fillers;
    fillers.reserve(64);
    unsigned index = 0;
    for (; index < 64; ++index) {
        std::snprintf(names[index], sizeof(names[index]), "filler%u", index);
        fillers.emplace_back(names[index], nullptr);
        const uint8_t before = action_type_count();
        action_type_register(&fillers[index]);
        if (action_type_count() == before) break;
    }
    check(index < 64, "registry reaches finite capacity");
    if (index >= 64) return;
    check(!action_type_find(names[index]), "overflow does not register action");
    check(registry_errors == 0, "startup overflow waits for logging");
    action_registry_logging_ready();
    check(registry_errors == 1 && strstr(registry_error, names[index]),
          "startup overflow reports rejected action");
    action_registry_logging_ready();
    check(registry_errors == 1, "startup overflow is reported once");
    action_type_register(&fillers[index]);
    check(registry_errors == 2 && strstr(registry_error, names[index]),
          "later overflow reports immediately");
    action_type_register(&fake_action_type);
    check(registry_errors == 2, "duplicate registration is not overflow");
}

int main() {
    test_operation_registry();
    test_registry_overflow_reporting();
    return failures ? 1 : 0;
}
