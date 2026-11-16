#include "item_binding_scheme.h"

#if HAS_DISPLAY && HAS_PSRAM
#include "log_manager.h"

bool item_binding_field_valid(const char* value, size_t length) {
    if (!value || !length || length > 32) return false;
    for (size_t i = 0; i < length; ++i) {
        const char c = value[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return true;
}

namespace {
const BindingParamDoc kParams[] = {
    {"field", "Field defined by the List's collection source.", nullptr, "^[A-Za-z0-9_]{1,32}$"},
};
const BindingKeyDoc kKeys[] = {{"*", "A field of the current List item.", nullptr}};
const BindingExampleDoc kExamples[] = {{"[item:id]", "The activated or rendered item's stable ID."}};
const BindingSchemeDoc kItemDoc = {
    "Data", "Read a field of the current List item.",
    BINDING_DOC_LIST(kParams), BINDING_DOC_LIST(kKeys), BINDING_DOC_LIST(kExamples),
    nullptr, false, BINDING_DOC_NONE,
    "Available only while rendering a List row or immediately activating it.", nullptr,
};
const BindingAuthoringKey kAuthoringKeys[] = {binding_authoring_text("*", "item")};
const BindingAuthoringSpec kAuthoring = {BINDING_DOC_LIST(kAuthoringKeys)};
}

void item_binding_init() {
    if (!binding_template_register("item", item_binding_resolve, nullptr,
        {1, 1, 1, -1, BINDING_VALIDATION_STANDARD, true, nullptr, nullptr,
         &kItemDoc, &kAuthoring, item_binding_field_valid})) {
        LOGE("ItemBind", "Failed to register item binding scheme");
    }
}
#endif
