#include "ereader_library.h"

#if HAS_EREADER
#include "ereader.h"
#include "list_source.h"
#include "log_manager.h"
#include "ota_activity.h"
#include <atomic>
#include <stdio.h>
#include <string.h>

namespace {
std::atomic<uint32_t> revision{0};
constexpr const char* directory = "/books";
constexpr const char* temporary = "/books/.upload.tmp";

void book_path(const char* name, char* path, size_t size) {
    snprintf(path, size, "/books/%s", name);
}

void release_upload(EreaderUpload& upload, bool remove_temporary) {
    if (!upload.active) return;
    if (upload.file) upload.file.close();
    if (remove_temporary && Storage.exists(temporary) && !Storage.remove(temporary))
        LOGE("Ereader", "Cannot remove incomplete E-book upload");
    upload.active = false;
    storage_mutation_finish();
    ereader_file_end();
}
}

uint32_t ereader_library_revision() { return revision.load(); }

EreaderFileResult ereader_library_list(bool (*visit)(const char*, size_t, void*), void* context) {
    if (!visit) return EreaderFileResult::Invalid;
    if (!ereader_available()) return EreaderFileResult::Unavailable;
    if (!ereader_catalog_begin()) return EreaderFileResult::Conflict;
    EreaderFileResult result = EreaderFileResult::Ok;
    {
        const StorageMutationGuard guard;
        if (!guard) result = EreaderFileResult::Conflict;
        else if (!storage_mount()) result = EreaderFileResult::StorageError;
        else if (Storage.exists(directory)) {
            File dir = Storage.open(directory, "r");
            if (!dir || !dir.isDirectory()) result = EreaderFileResult::StorageError;
            else {
                size_t count = 0;
                for (File file = dir.openNextFile(); file; file = dir.openNextFile()) {
                    const char* name = strrchr(file.name(), '/');
                    name = name ? name + 1 : file.name();
                    if (!file.isDirectory() && ereader_valid_book_name(name)) {
                        if (++count > LIST_SOURCE_MAX_ITEMS || !visit(name, file.size(), context))
                            result = EreaderFileResult::StorageError;
                    }
                    file.close();
                    if (result != EreaderFileResult::Ok) break;
                }
                dir.close();
            }
        }
    }
    ereader_file_end();
    return result;
}

EreaderFileResult ereader_library_delete(const char* name) {
    if (!ereader_valid_book_name(name)) return EreaderFileResult::Invalid;
    if (!ereader_available()) return EreaderFileResult::Unavailable;
    if (!ereader_file_begin(name, true)) return EreaderFileResult::Conflict;
    EreaderFileResult result = EreaderFileResult::Ok;
    {
        const StorageMutationGuard guard;
        char path[72];
        book_path(name, path, sizeof(path));
        if (!guard) result = EreaderFileResult::Conflict;
        else if (!storage_mount()) result = EreaderFileResult::StorageError;
        else if (!Storage.exists(path)) result = EreaderFileResult::NotFound;
        else {
            File file = Storage.open(path, "r");
            if (!file) result = EreaderFileResult::StorageError;
            else {
                const bool is_directory = file.isDirectory();
                file.close();
                if (is_directory) result = EreaderFileResult::NotFound;
                else if (!Storage.remove(path)) result = EreaderFileResult::StorageError;
                else {
                    ereader_file_removed(name);
                    ++revision;
                }
            }
        }
    }
    ereader_file_end();
    if (result == EreaderFileResult::Ok) storage_publish_usage();
    return result;
}

EreaderFileResult ereader_upload_begin(EreaderUpload& upload, const char* name) {
    if (!ereader_valid_book_name(name)) return EreaderFileResult::Invalid;
    if (!ereader_available()) return EreaderFileResult::Unavailable;
    if (!ereader_file_begin(name, false)) return EreaderFileResult::Conflict;
    StorageMutationGuard guard;
    EreaderFileResult result = EreaderFileResult::Ok;
    char path[72];
    book_path(name, path, sizeof(path));
    if (!guard) result = EreaderFileResult::Conflict;
    else if (!storage_mount()) result = EreaderFileResult::StorageError;
    else if (!Storage.exists(directory) && !Storage.mkdir(directory)) result = EreaderFileResult::StorageError;
    else if (Storage.exists(path)) result = EreaderFileResult::Conflict;
    else if (Storage.exists(temporary) && !Storage.remove(temporary)) result = EreaderFileResult::StorageError;
    else {
        upload.file = Storage.open(temporary, "w");
        if (!upload.file) result = EreaderFileResult::StorageError;
        else {
            strlcpy(upload.name, name, sizeof(upload.name));
            upload.received = 0;
            upload.active = true;
            guard.release();
        }
    }
    if (result != EreaderFileResult::Ok) ereader_file_end();
    return result;
}

EreaderFileResult ereader_upload_write(EreaderUpload& upload, const uint8_t* data, size_t length) {
    if (!upload.active) return EreaderFileResult::Conflict;
    if (ota_activity_is_active() || length > SIZE_MAX - upload.received ||
        upload.file.write(data, length) != length) {
        release_upload(upload, true);
        return EreaderFileResult::StorageError;
    }
    upload.received += length;
    return EreaderFileResult::Ok;
}

EreaderFileResult ereader_upload_finish(EreaderUpload& upload, size_t expected) {
    if (!upload.active) return EreaderFileResult::Conflict;
    upload.file.flush();
    upload.file.close();
    File file = Storage.open(temporary, "r");
    EreaderFileResult result = EreaderFileResult::Ok;
    if (!file || file.isDirectory() || file.size() != expected || upload.received != expected)
        result = EreaderFileResult::StorageError;
    if (file) file.close();
    char path[72];
    book_path(upload.name, path, sizeof(path));
    if (result == EreaderFileResult::Ok && Storage.exists(path)) result = EreaderFileResult::Conflict;
    if (result == EreaderFileResult::Ok && !Storage.rename(temporary, path))
        result = EreaderFileResult::StorageError;
    if (result == EreaderFileResult::Ok) ++revision;
    release_upload(upload, result != EreaderFileResult::Ok);
    if (result == EreaderFileResult::Ok) storage_publish_usage();
    return result;
}

void ereader_upload_abort(EreaderUpload& upload) { release_upload(upload, true); }
#endif
