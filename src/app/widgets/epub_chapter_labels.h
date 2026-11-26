#pragma once

#include <FreeInkBook.h>
#include <string.h>

namespace epub_reader_impl {

inline const char* chapter_navigation_label(freeink::book::BookSource& source,
                                            const freeink::book::TocEntry* entries,
                                            size_t count, const char* path, bool& interrupted) {
    const freeink::book::TocEntry* match = nullptr;
    for (size_t i = 0; i < count; ++i) {
        if (i % 32 == 0 && source.shouldStop()) {
            interrupted = true;
            return nullptr;
        }
        const auto& entry = entries[i];
        if (!entry.href || strcmp(entry.href, path)) continue;
        if (match) return nullptr;
        match = &entry;
    }
    return match && !match->fragment && match->title && match->title[0] ? match->title : nullptr;
}

}
