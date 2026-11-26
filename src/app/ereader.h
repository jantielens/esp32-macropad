#pragma once

#include "board_config.h"

#if HAS_EREADER
#include <stddef.h>
#include <stdint.h>

enum class EreaderCommand {
    PreviousPage,
    NextPage,
    PreviousChapter,
    NextChapter,
};

enum class EreaderMetadataState : uint8_t { Idle, Loading, Ready, Error };

struct EreaderSnapshot {
    char book[64];
    char error[128];
    char warning[128];
    char chapter_error[128];
    uint32_t chapter;
    uint32_t chapter_count;
    uint32_t page;
    uint32_t page_count;
    bool visible;
    bool loading;
    bool failed;
    bool chapters_available;
    bool page_available;
    EreaderMetadataState chapter_state;
};

bool ereader_init();
bool ereader_available();
bool ereader_valid_book_name(const char* name);
bool ereader_open(const char* name);
bool ereader_navigate(EreaderCommand command);
bool ereader_open_chapter(uint32_t chapter, const char* expected_book = nullptr);
bool ereader_chapter_title(const char* book, uint32_t chapter, char* out, size_t size);
void ereader_snapshot(EreaderSnapshot* snapshot);
uint32_t ereader_chapters_revision();
bool ereader_file_begin(const char* name, bool deleting);
bool ereader_catalog_begin();
void ereader_file_removed(const char* name);
void ereader_file_end();
#endif
