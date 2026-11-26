#include "ereader_library.h"
#include "ereader.h"
#include <cassert>
#include <cstring>
#include <vector>

FakeFontStorage LittleFS;
FakeFontStorage SD_MMC;
static bool reserved = false;
static bool in_use = false;
static bool ota = false;
static unsigned writers = 0;
static std::string removed;

bool ereader_available() { return true; }
bool ereader_valid_book_name(const char* name) {
    return name && !strchr(name, '/') && strlen(name) >= 6 &&
           !strcmp(name + strlen(name) - 5, ".epub");
}
bool ereader_file_begin(const char*, bool deleting) {
    if (reserved || ota || (deleting && in_use)) return false;
    reserved = true;
    return true;
}
bool ereader_catalog_begin() {
    if (reserved || ota) return false;
    reserved = true;
    return true;
}
void ereader_file_end() { assert(reserved); reserved = false; }
void ereader_file_removed(const char* name) { assert(reserved); removed = name; }
bool ota_activity_is_active() { return ota; }
bool ota_activity_try_begin_config_write() { if (ota) return false; ++writers; return true; }
void ota_activity_finish_config_write() { assert(writers); --writers; }
bool storage_mount() { return Storage.mounted; }
void storage_publish_usage(bool) {}

static bool collect(const char* name, size_t size, void* context) {
    auto& names = *static_cast<std::vector<std::string>*>(context);
    names.emplace_back(name);
    assert(size == 4);
    return true;
}

int main() {
    const uint8_t bytes[] = {1, 2, 3, 4};
    EreaderUpload upload;
    assert(ereader_upload_begin(upload, "Alice.epub") == EreaderFileResult::Ok);
    assert(reserved && writers == 1);
    assert(ereader_library_delete("Alice.epub") == EreaderFileResult::Conflict);
    assert(ereader_upload_write(upload, bytes, sizeof(bytes)) == EreaderFileResult::Ok);
    assert(ereader_upload_finish(upload, sizeof(bytes)) == EreaderFileResult::Ok);
    assert(!reserved && writers == 0);
    assert(Storage.files["/books/Alice.epub"] == std::vector<uint8_t>(bytes, bytes + sizeof(bytes)));
    assert(ereader_upload_begin(upload, "Alice.epub") == EreaderFileResult::Conflict);
    std::vector<std::string> names;
    assert(ereader_library_list(collect, &names) == EreaderFileResult::Ok);
    assert(names.size() == 1 && names[0] == "Alice.epub");
    in_use = true;
    assert(ereader_library_delete("Alice.epub") == EreaderFileResult::Conflict);
    in_use = false;
    assert(ereader_library_delete("Alice.epub") == EreaderFileResult::Ok);
    assert(removed == "Alice.epub");
    assert(ereader_library_delete("Alice.epub") == EreaderFileResult::NotFound);
    assert(ereader_upload_begin(upload, "unfinished.epub") == EreaderFileResult::Ok);
    assert(ereader_upload_write(upload, bytes, 2) == EreaderFileResult::Ok);
    ereader_upload_abort(upload);
    assert(!reserved && writers == 0 && !Storage.exists("/books/.upload.tmp"));
    assert(ereader_upload_begin(upload, "short.epub") == EreaderFileResult::Ok);
    Storage.short_write = true;
    assert(ereader_upload_write(upload, bytes, sizeof(bytes)) == EreaderFileResult::StorageError);
    Storage.short_write = false;
    assert(!reserved && writers == 0 && !Storage.exists("/books/.upload.tmp"));
    assert(ereader_upload_begin(upload, "incomplete.epub") == EreaderFileResult::Ok);
    assert(ereader_upload_write(upload, bytes, 2) == EreaderFileResult::Ok);
    assert(ereader_upload_finish(upload, 4) == EreaderFileResult::StorageError);
    assert(!reserved && writers == 0 && !Storage.exists("/books/incomplete.epub"));
    assert(ereader_upload_begin(upload, "rename.epub") == EreaderFileResult::Ok);
    assert(ereader_upload_write(upload, bytes, sizeof(bytes)) == EreaderFileResult::Ok);
    Storage.rename_failure = true;
    assert(ereader_upload_finish(upload, sizeof(bytes)) == EreaderFileResult::StorageError);
    Storage.rename_failure = false;
    assert(!reserved && writers == 0 && !Storage.exists("/books/.upload.tmp"));
    assert(ereader_upload_begin(upload, "ota.epub") == EreaderFileResult::Ok);
    ota = true;
    assert(ereader_upload_write(upload, bytes, sizeof(bytes)) == EreaderFileResult::StorageError);
    ota = false;
    assert(!reserved && writers == 0 && !Storage.exists("/books/.upload.tmp"));
}
