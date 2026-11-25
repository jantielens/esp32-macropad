#pragma once

#include "../storage.h"
#include <BookStorage.h>

namespace epub_reader_impl {

class CacheStorageAdapter : public freeink::book::CacheStorage {
public:
    CacheStorageAdapter(uint32_t book_key, uint32_t generation, uint8_t* buffer, size_t capacity)
        : book_key_(book_key), generation_(generation), buffer_(buffer), capacity_(capacity) {}

    ~CacheStorageAdapter() { abort(); }

    bool exists(const char* name) override {
        char path[96];
        if (!make_path(name, path, sizeof(path))) return false;
        const StorageMutationGuard guard;
        return guard && current(generation_) && Storage.exists(path);
    }

    bool remove(const char* name) override {
        char path[96];
        if (!make_path(name, path, sizeof(path))) return false;
        const StorageMutationGuard guard;
        if (!guard || !current(generation_)) return false;
        if (writing_) {
            writing_ = false;
            used_ = 0;
            return !Storage.exists(temporary_) || Storage.remove(temporary_);
        }
        return !Storage.exists(path) || Storage.remove(path);
    }

    int64_t fileSize(const char* name) override {
        char path[96];
        if (!make_path(name, path, sizeof(path))) return -1;
        const StorageMutationGuard guard;
        if (!guard || !current(generation_)) return -1;
        File file = Storage.open(path, "r");
        if (!file || file.isDirectory()) return -1;
        const int64_t size = file.size();
        file.close();
        return size;
    }

    int32_t readAt(const char* name, uint32_t offset, void* dst, uint32_t len) override {
        char path[96];
        if (!make_path(name, path, sizeof(path))) return -1;
        if (!current(generation_) || ota_activity_is_active()) return -1;
        if (!len) return 0;
        if (strcmp(path, read_path_) || offset < read_offset_ ||
            static_cast<uint64_t>(offset) >= static_cast<uint64_t>(read_offset_) + read_length_) {
            const int32_t count = read_file(path, offset, buffer_, capacity_);
            if (count <= 0) return count;
            strlcpy(read_path_, path, sizeof(read_path_));
            read_offset_ = offset;
            read_length_ = static_cast<size_t>(count);
        }
        const size_t start = offset - read_offset_;
        const size_t count = len < read_length_ - start ? len : read_length_ - start;
        memcpy(dst, buffer_ + start, count);
        return static_cast<int32_t>(count);
    }

    bool beginWrite(const char* name) override {
        if (writing_ || !buffer_ || !capacity_ || !make_path(name, final_, sizeof(final_)) ||
            snprintf(temporary_, sizeof(temporary_), "%s.tmp", final_) >= static_cast<int>(sizeof(temporary_)) ||
            snprintf(backup_, sizeof(backup_), "%s.bak", final_) >= static_cast<int>(sizeof(backup_))) return false;
        const StorageMutationGuard guard;
        if (!guard || !current(generation_)) return false;
        if (Storage.exists(backup_) && !Storage.exists(final_) && !Storage.rename(backup_, final_)) return false;
        if ((Storage.exists(backup_) && !Storage.remove(backup_)) ||
            (Storage.exists(temporary_) && !Storage.remove(temporary_))) return false;
        File file = Storage.open(temporary_, "w");
        if (!file) return false;
        file.close();
        used_ = 0;
        read_length_ = 0;
        read_path_[0] = 0;
        writing_ = true;
        failed_ = false;
        return true;
    }

    bool write(const void* data, uint32_t len) override {
        if (!writing_ || failed_) return false;
        const auto* bytes = static_cast<const uint8_t*>(data);
        while (len) {
            if (!current(generation_) || ota_activity_is_active()) return fail();
            const size_t count = len < capacity_ - used_ ? len : capacity_ - used_;
            memcpy(buffer_ + used_, bytes, count);
            used_ += count;
            bytes += count;
            len -= count;
            if (used_ == capacity_ && !flush()) return false;
        }
        return true;
    }

    bool endWrite() override {
        if (!writing_ || failed_ || !flush()) return false;
        const StorageMutationGuard guard;
        if (!guard || !current(generation_)) return fail();
        const bool previous = Storage.exists(final_);
        if (previous && !Storage.rename(final_, backup_)) return fail();
        if (!Storage.rename(temporary_, final_)) {
            if (previous && !Storage.rename(backup_, final_))
                LOGE("EpubReader", "Could not restore page cache after rename failure");
            return fail();
        }
        writing_ = false;
        if (previous && !Storage.remove(backup_)) {
            LOGW("EpubReader", "Could not remove page cache backup");
        }
        return true;
    }

    int32_t readBackAt(uint32_t offset, void* dst, uint32_t len) override {
        if (!writing_ || failed_ || !flush()) return -1;
        return read_file(temporary_, offset, dst, len);
    }

    void abort() {
        if (!writing_) return;
        used_ = 0;
        const StorageMutationGuard guard;
        if (!guard)
            LOGW("EpubReader", "Incomplete page cache cleanup deferred");
        else if (Storage.exists(temporary_) && !Storage.remove(temporary_))
            LOGW("EpubReader", "Could not remove incomplete page cache");
        writing_ = false;
    }

private:
    bool make_path(const char* name, char* out, size_t size) const {
        if (!name || strchr(name, '/') || strchr(name, '\\')) return false;
        const int n = snprintf(out, size, "/storage/.epub-%08lx-%s",
                               static_cast<unsigned long>(book_key_), name);
        return n > 0 && static_cast<size_t>(n) < size;
    }

    int32_t read_file(const char* path, uint32_t offset, void* dst, uint32_t len) {
        if (len > INT32_MAX) return -1;
        const StorageMutationGuard guard;
        if (!guard || !current(generation_)) return -1;
        File file = Storage.open(path, "r");
        if (!file || !file.seek(offset)) return -1;
        const size_t count = file.read(static_cast<uint8_t*>(dst), len);
        file.close();
        taskYIELD();
        return static_cast<int32_t>(count);
    }

    bool flush() {
        if (!used_) return true;
        const StorageMutationGuard guard;
        if (!guard || !current(generation_)) return fail();
        File file = Storage.open(temporary_, "a");
        if (!file) return fail();
        const size_t written = file.write(buffer_, used_);
        file.flush();
        file.close();
        if (written != used_) return fail();
        used_ = 0;
        taskYIELD();
        return true;
    }

    bool fail() { failed_ = true; return false; }

    uint32_t book_key_;
    uint32_t generation_;
    uint8_t* buffer_;
    size_t capacity_;
    size_t used_ = 0;
    size_t read_length_ = 0;
    uint32_t read_offset_ = 0;
    bool writing_ = false;
    bool failed_ = false;
    char final_[96]{};
    char temporary_[104]{};
    char backup_[104]{};
    char read_path_[96]{};
};

}  // namespace epub_reader_impl
