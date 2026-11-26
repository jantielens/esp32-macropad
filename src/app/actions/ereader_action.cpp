#include "action_registry.h"

#if HAS_EREADER
#include "../ereader.h"
#include "../execution_context.h"
#include "../item_binding.h"
#include "../list_source.h"
#include "../log_manager.h"

namespace {
const char* validate_ereader_book(const char* name) {
    return ereader_valid_book_name(name) ? nullptr : "book must be an EPUB filename";
}

ActionResult dispatch_ereader_open(const ActionResolvedArgument* args, const char* label, uint32_t) {
    if (ereader_open(args[0].text)) return ACTION_COMPLETE;
    LOGW("Ereader", "%s: open rejected", label);
    return ACTION_FAILED;
}

ActionResult dispatch_ereader_navigation(const ActionResolvedArgument* args, const char* label, uint32_t) {
    EreaderCommand command;
    if (!strcmp(args[0].text, "previous_page")) command = EreaderCommand::PreviousPage;
    else if (!strcmp(args[0].text, "next_page")) command = EreaderCommand::NextPage;
    else if (!strcmp(args[0].text, "previous_chapter")) command = EreaderCommand::PreviousChapter;
    else if (!strcmp(args[0].text, "next_chapter")) command = EreaderCommand::NextChapter;
    else return ACTION_FAILED;
    if (ereader_navigate(command)) return ACTION_COMPLETE;
    LOGW("Ereader", "%s: navigation rejected", label);
    return ACTION_FAILED;
}

ActionResult dispatch_ereader_chapter(const ActionResolvedArgument* args, const char* label, uint32_t) {
    const char* expected_book = nullptr;
    const ItemBindingScope* item = thread_execution_context.item_scope;
    if (item && item->source() && !strcmp(item->source()->token, "[ereader:chapters]")) {
        expected_book = item->item()["book"] | "";
        if (!expected_book[0]) {
            LOGW("Ereader", "%s: chapter item has no book identity", label);
            return ACTION_FAILED;
        }
    }
    if (ereader_open_chapter(static_cast<uint32_t>(args[0].integer), expected_book)) return ACTION_COMPLETE;
    LOGW("Ereader", "%s: chapter selection rejected", label);
    return ACTION_FAILED;
}

const char* const ereader_commands[] = {"previous_page", "next_page", "previous_chapter", "next_chapter"};
const char* const ereader_command_labels[] = {"Previous page", "Next page", "Previous chapter", "Next chapter"};
const ActionArgumentDef ereader_open_arguments[] = {
    {"book", "Book", ACTION_ARGUMENT_STRING, 0, 0, 63, true, "Alice.epub", "text",
     nullptr, 0, false, validate_ereader_book},
};
const ActionArgumentDef ereader_navigation_arguments[] = {
    {"command", "Command", ACTION_ARGUMENT_ENUM, 0, 0, 16, true, "next_page", "select",
     ereader_commands, 4, false, nullptr, ereader_command_labels},
};
const ActionArgumentDef ereader_chapter_arguments[] = {
    {"chapter", "Chapter", ACTION_ARGUMENT_INTEGER, 1, INT32_MAX, 0, true, "1", "number"},
};
const ActionOperationDef ereader_operations[] = {
    {"ereader.open", "Open E-book", ereader_open_arguments, 1, dispatch_ereader_open},
    {"ereader.navigate", "Navigate E-reader", ereader_navigation_arguments, 1, dispatch_ereader_navigation},
    {"ereader.open_chapter", "Open chapter", ereader_chapter_arguments, 1, dispatch_ereader_chapter},
};
void describe_ereader(JsonObject& action) {
    action["group"] = "E-reader";
    action["label"] = "E-reader";
}
DEFINE_AND_REGISTER_ACTION_TYPE(kEreaderAction, ACTION_TYPE_EREADER, describe_ereader, nullptr,
    ACTION_EXECUTION_SYNC, true, true, ereader_operations, 3);
}
#endif
