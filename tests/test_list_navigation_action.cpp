#include <gtest/gtest.h>
#include "action_registry.h"
#include "device_dialog.h"

#define DISPLAY_MANAGER_H
enum DisplayTaskDispatchResult { DISPLAY_TASK_DISPATCH_OK, DISPLAY_TASK_DISPATCH_INVALID };
using DisplayTaskExec = void (*)(const void*, bool*, char*, size_t);
using DisplayTaskCleanup = void (*)(const void*);

static bool on_lvgl_task = false;
static bool capture_ok = true;
static bool navigation_ok = true;
static uint32_t dialog_generation = 1;
static uint32_t pad_generation = 1;
static unsigned dispatches = 0;
static unsigned navigations = 0;
static unsigned button_navigations = 0;
static bool change_generation = false;
static bool change_dialog_generation = false;

struct DisplayManager {
    bool isInLvglTask() const { return on_lvgl_task; }
    bool captureListNavigation(uint32_t* generation) {
        *generation = 7;
        return capture_ok;
    }
    bool navigateList(const char* target, const char* command, uint32_t generation) {
        EXPECT_STREQ(target, "notes");
        EXPECT_STREQ(command, "next_page");
        EXPECT_EQ(generation, 7u);
        ++navigations;
        return navigation_ok;
    }
    bool navigateButtons(const char* command) {
        EXPECT_STREQ(command, "next");
        ++button_navigations;
        return navigation_ok;
    }
};

static DisplayManager manager;
DisplayManager* displayManager = &manager;
uint32_t device_dialog_input_generation() { return dialog_generation; }
uint32_t pad_config_get_generation() { return pad_generation; }
void action_type_register(const ActionTypeDef*) {}

DisplayTaskDispatchResult display_manager_dispatch(
        DisplayTaskExec exec, DisplayTaskCleanup, const void* context, size_t,
        uint32_t, bool* ok, char*, size_t) {
    ++dispatches;
    if (change_generation) ++pad_generation;
    if (change_dialog_generation) ++dialog_generation;
    exec(context, ok, nullptr, 0);
    return DISPLAY_TASK_DISPATCH_OK;
}

#define ARDUINO 1
#include "../src/app/actions/list_navigation_action.cpp"
#include "../src/app/actions/button_navigation_action.cpp"
#undef ARDUINO

void ItemBindingScope::end_current() {}

class ListNavigationAction : public testing::Test {
protected:
    ButtonAction action{};
    void SetUp() override {
        on_lvgl_task = false;
        capture_ok = navigation_ok = true;
        dialog_generation = pad_generation = 1;
        dispatches = navigations = button_navigations = 0;
        change_generation = change_dialog_generation = false;
        displayManager = &manager;
        strlcpy(action.payload.list_navigation.target, "notes", sizeof(action.payload.list_navigation.target));
        strlcpy(action.payload.list_navigation.command, "next_page", sizeof(action.payload.list_navigation.command));
    }
};

TEST_F(ListNavigationAction, ExecutesOnLvglTaskWithoutDispatching) {
    on_lvgl_task = true;
    EXPECT_EQ(dispatch_list_navigation(action, "", 0), ACTION_COMPLETE);
    EXPECT_EQ(dispatches, 0u);
    EXPECT_EQ(navigations, 1u);
    navigation_ok = false;
    EXPECT_EQ(dispatch_list_navigation(action, "", 0), ACTION_FAILED);
}

TEST_F(ListNavigationAction, DispatchesFromOtherTasksAndRejectsStalePad) {
    EXPECT_EQ(dispatch_list_navigation(action, "", 0), ACTION_COMPLETE);
    EXPECT_EQ(dispatches, 1u);
    EXPECT_EQ(navigations, 1u);
    change_generation = true;
    EXPECT_EQ(dispatch_list_navigation(action, "", 0), ACTION_FAILED);
    EXPECT_EQ(navigations, 1u);
    capture_ok = false;
    EXPECT_EQ(dispatch_list_navigation(action, "", 0), ACTION_FAILED);
    EXPECT_EQ(dispatches, 2u);
}

TEST_F(ListNavigationAction, OperationDispatchesAndPropagatesRejections) {
    const ActionResolvedArgument arguments[] = {{"notes", 0, 0}, {"next_page", 0, 0}};
    EXPECT_EQ(kListNavigationOperations[0].dispatch(arguments, "", 0), ACTION_COMPLETE);
    EXPECT_EQ(dispatches, 1u);
    EXPECT_EQ(navigations, 1u);
    navigation_ok = false;
    EXPECT_EQ(kListNavigationOperations[0].dispatch(arguments, "", 0), ACTION_FAILED);
    change_generation = true;
    EXPECT_EQ(kListNavigationOperations[0].dispatch(arguments, "", 0), ACTION_FAILED);
    EXPECT_EQ(navigations, 2u);
}

TEST(ListNavigationValidation, RejectsInvalidTargets) {
    EXPECT_EQ(validate_list_target("notes-2"), nullptr);
    EXPECT_NE(validate_list_target(""), nullptr);
    EXPECT_NE(validate_list_target("notes/2"), nullptr);
    EXPECT_STREQ(kListNavigationArguments[0].control, "list_ids");
}

TEST_F(ListNavigationAction, ButtonOperationDispatchesAndRejectsStaleDialog) {
    const ActionResolvedArgument arguments[] = {{"next", 0, 0}};
    EXPECT_EQ(kButtonNavigationOperations[0].dispatch(arguments, "", 0), ACTION_COMPLETE);
    EXPECT_EQ(button_navigations, 1u);
    navigation_ok = false;
    EXPECT_EQ(kButtonNavigationOperations[0].dispatch(arguments, "", 0), ACTION_FAILED);
    change_dialog_generation = true;
    EXPECT_EQ(kButtonNavigationOperations[0].dispatch(arguments, "", 0), ACTION_FAILED);
    EXPECT_EQ(button_navigations, 2u);
}
