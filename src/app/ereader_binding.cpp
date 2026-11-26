#include "ereader_binding.h"

#if HAS_EREADER
#include "binding_template.h"
#include "ereader.h"
#include "log_manager.h"
#include <stdio.h>
#include <string.h>

namespace {
const char* const keys[] = {
    "state", "book", "chapter", "chapter_count", "page", "page_count", "error", "warning",
    "chapter_state", "chapter_error"
};
const BindingParamDoc params[] = {{"key", "E-reader state or current position key."}};
const BindingKeyDoc key_docs[] = {
    {"state", "idle, loading, ready, or error", nullptr},
    {"book", "Selected E-book filename", nullptr},
    {"chapter", "Current one-based reading-order chapter", nullptr},
    {"chapter_count", "Number of reading-order entries", nullptr},
    {"page", "Current one-based page in this chapter", nullptr},
    {"page_count", "Page count in this chapter", nullptr},
    {"error", "Current reader failure", nullptr},
    {"warning", "Nonfatal warning or boundary notice", nullptr},
    {"chapter_state", "Chapter metadata: idle, loading, ready, or error", nullptr},
    {"chapter_error", "Chapter metadata failure", nullptr},
};
const BindingExampleDoc examples[] = {
    {"[ereader:state]", "Show the reader state."},
    {"[ereader:warning|]", "Show a warning when present."},
    {"Chapter [ereader:chapter|?], page [ereader:page|?]", "Show the current position."},
};
const BindingSchemeDoc kEreaderDoc = {
    "Device", "E-reader session status and current-chapter position.",
    BINDING_DOC_LIST(params), BINDING_DOC_LIST(key_docs), BINDING_DOC_LIST(examples),
    nullptr, false, BINDING_DOC_NONE, nullptr, nullptr,
};
const BindingAuthoringKey authoring_keys[] = {
    binding_authoring_text("state", "ready"),
    binding_authoring_text("book", "Alice.epub"),
    binding_authoring_integer("chapter", "", "1"),
    binding_authoring_integer("chapter_count", "", "12"),
    binding_authoring_integer("page", "", "1"),
    binding_authoring_integer("page_count", "", "8"),
    binding_authoring_text("error", "Cannot open EPUB"),
    binding_authoring_text("warning", "Some glyphs unavailable"),
    binding_authoring_text("chapter_state", "ready"),
    binding_authoring_text("chapter_error", "Cannot open EPUB"),
};
const BindingAuthoringSpec authoring = {BINDING_DOC_LIST(authoring_keys)};
uint8_t key_count() { return sizeof(keys) / sizeof(keys[0]); }
const char* key_at(uint8_t index) { return index < key_count() ? keys[index] : nullptr; }

BindingResolverStatus resolve(const char* key, char* out, size_t size) {
    if (!key || !out || !size) return BINDING_RESOLVER_UNAVAILABLE;
    EreaderSnapshot snapshot{};
    ereader_snapshot(&snapshot);
    const char* text = nullptr;
    uint32_t number = 0;
    if (!strcmp(key, "state"))
        text = snapshot.failed ? "error" : snapshot.loading ? "loading" :
               snapshot.visible && snapshot.book[0] ? "ready" : "idle";
    else if (!strcmp(key, "book")) text = snapshot.book;
    else if (!strcmp(key, "error")) text = snapshot.error;
    else if (!strcmp(key, "warning")) text = snapshot.warning;
    else if (!strcmp(key, "chapter_state")) {
        switch (snapshot.chapter_state) {
            case EreaderMetadataState::Idle: text = "idle"; break;
            case EreaderMetadataState::Loading: text = "loading"; break;
            case EreaderMetadataState::Ready: text = "ready"; break;
            case EreaderMetadataState::Error: text = "error"; break;
        }
    }
    else if (!strcmp(key, "chapter_error")) text = snapshot.chapter_error;
    else if (!strcmp(key, "chapter")) number = snapshot.chapter;
    else if (!strcmp(key, "chapter_count")) number = snapshot.chapters_available ? snapshot.chapter_count : 0;
    else if (!strcmp(key, "page")) number = snapshot.page_available ? snapshot.page : 0;
    else if (!strcmp(key, "page_count")) number = snapshot.page_available ? snapshot.page_count : 0;
    else return BINDING_RESOLVER_UNKNOWN;
    if (text) {
        if (!text[0]) return BINDING_RESOLVER_UNAVAILABLE;
        strlcpy(out, text, size);
    } else {
        if (!number) return BINDING_RESOLVER_UNAVAILABLE;
        snprintf(out, size, "%lu", static_cast<unsigned long>(number));
    }
    return BINDING_RESOLVER_RESOLVED;
}
}

void ereader_binding_init() {
    if (!binding_template_register("ereader", resolve, nullptr,
        {1, 1, 1, -1, BINDING_VALIDATION_STANDARD, false, key_count, key_at, &kEreaderDoc, &authoring}))
        LOGE("Ereader", "Cannot register E-reader binding");
}
#endif
