#pragma once

#include "board_config.h"

#if HAS_EREADER
#include "storage.h"
#include <stddef.h>
#include <stdint.h>

enum class EreaderFileResult { Ok, Invalid, NotFound, Conflict, Unavailable, StorageError };
struct EreaderUpload {
    File file;
    char name[64]{};
    size_t received = 0;
    bool active = false;
};

uint32_t ereader_library_revision();
EreaderFileResult ereader_library_list(bool (*visit)(const char*, size_t, void*), void* context);
EreaderFileResult ereader_library_delete(const char* name);
EreaderFileResult ereader_upload_begin(EreaderUpload& upload, const char* name);
EreaderFileResult ereader_upload_write(EreaderUpload& upload, const uint8_t* data, size_t length);
EreaderFileResult ereader_upload_finish(EreaderUpload& upload, size_t expected);
void ereader_upload_abort(EreaderUpload& upload);
#endif
