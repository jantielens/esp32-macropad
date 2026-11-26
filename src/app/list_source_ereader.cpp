#include "list_source_ereader.h"

#if HAS_EREADER
#include "ereader.h"
#include "ereader_library.h"
#include "list_source.h"
#include "log_manager.h"
#include <stdio.h>
#include <string.h>

namespace {
struct BookVisitor {
    ListSourceSnapshot* snapshot;
    const char* error;
};

bool append_book(const char* name, size_t size, void* context) {
    auto& visitor = *static_cast<BookVisitor*>(context);
    BoundedPsramJsonAllocator allocator(8 * 1024);
    JsonDocument row(&allocator);
    char title[64];
    strlcpy(title, name, sizeof(title));
    title[strlen(title) - 5] = '\0';
    row["id"] = name;
    row["title"] = title;
    row["size_bytes"] = size;
    if (row.overflowed()) visitor.error = "E-book metadata allocation failed";
    else visitor.error = visitor.snapshot->append(row.as<JsonObjectConst>());
    return !visitor.error;
}

ListSourceLoadStep books_step(void**, ListSourceSnapshot* snapshot, const char** error) {
    BookVisitor visitor{snapshot, nullptr};
    const EreaderFileResult result = ereader_library_list(append_book, &visitor);
    if (result == EreaderFileResult::Conflict) return ListSourceLoadStep::Deferred;
    if (result != EreaderFileResult::Ok) {
        *error = visitor.error ? visitor.error : "E-book library unavailable or too large";
        return ListSourceLoadStep::Failed;
    }
    return ListSourceLoadStep::Complete;
}

ListSourceLoadStep chapters_step(void**, ListSourceSnapshot* snapshot, const char** error) {
    EreaderSnapshot reader{};
    ereader_snapshot(&reader);
    if (!reader.chapters_available) return ListSourceLoadStep::Complete;
    if (reader.chapter_count > LIST_SOURCE_MAX_ITEMS) {
        *error = "E-book has too many chapters";
        return ListSourceLoadStep::Failed;
    }
    BoundedPsramJsonAllocator allocator(8 * 1024);
    JsonDocument row(&allocator);
    for (uint32_t chapter = 1; chapter <= reader.chapter_count; ++chapter) {
        char id[16], title[32];
        snprintf(id, sizeof(id), "%lu", static_cast<unsigned long>(chapter));
        char chapter_title[LIST_SOURCE_MAX_TEXT_BYTES + 1];
        if (!ereader_chapter_title(reader.book, chapter, chapter_title, sizeof(chapter_title)))
            return ListSourceLoadStep::Deferred;
        if (!chapter_title[0]) snprintf(title, sizeof(title), "Chapter %lu", static_cast<unsigned long>(chapter));
        row.clear();
        row["id"] = id;
        row["title"] = chapter_title[0] ? chapter_title : title;
        row["chapter"] = chapter;
        row["book"] = reader.book;
        if (row.overflowed()) *error = "chapter metadata allocation failed";
        else *error = snapshot->append(row.as<JsonObjectConst>());
        if (*error) return ListSourceLoadStep::Failed;
    }
    return ListSourceLoadStep::Complete;
}

void finish(void*) {}
const ListSourceField book_fields[] = {
    {"id", "E-book filename", ListSourceFieldType::String, false},
    {"title", "Filename without the .epub extension", ListSourceFieldType::String, false},
    {"size_bytes", "File size in bytes", ListSourceFieldType::Number, false},
};
const ListSourceField chapter_fields[] = {
    {"id", "One-based chapter number", ListSourceFieldType::String, false},
    {"title", "Matching navigation label, or Chapter N", ListSourceFieldType::String, false},
    {"chapter", "One-based chapter number", ListSourceFieldType::Number, false},
    {"book", "Selected E-book filename", ListSourceFieldType::String, false},
};
const ListSourceDescriptor books{"[ereader:books]", "E-books", book_fields, 3,
    ereader_library_revision, books_step, finish};
const ListSourceDescriptor chapters{"[ereader:chapters]", "E-book chapters", chapter_fields, 4,
    ereader_chapters_revision, chapters_step, finish};
}

bool list_source_ereader_init() {
    const bool registered_books = list_source_register(&books);
    const bool registered_chapters = list_source_register(&chapters);
    return registered_books && registered_chapters;
}
#endif
