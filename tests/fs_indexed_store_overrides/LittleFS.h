#pragma once

#include <Arduino.h>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

class FakeIndexedStorage;

class File {
public:
    File() = default;
    File(FakeIndexedStorage* storage, const std::string& path, bool directory);
    explicit operator bool() const { return valid_; }
    bool isDirectory() const { return directory_; }
    const char* name() const {
        size_t slash = path_.find_last_of('/');
        return path_.c_str() + (slash == std::string::npos ? 0 : slash + 1);
    }
    size_t size() const;
    time_t getLastWrite() const { return 0; }
    size_t read(uint8_t* data, size_t length);
    int read() {
        uint8_t byte;
        return read(&byte, 1) == 1 ? byte : -1;
    }
    size_t readBytes(char* data, size_t length) {
        return read(reinterpret_cast<uint8_t*>(data), length);
    }
    size_t position() const { return position_; }
    int available() const { return valid_ && position_ < size() ? static_cast<int>(size() - position_) : 0; }
    size_t readBytesUntil(char terminator, char* data, size_t length) {
        size_t count = 0;
        while (count < length) {
            const int byte = read();
            if (byte < 0 || byte == terminator) break;
            data[count++] = static_cast<char>(byte);
        }
        return count;
    }
    size_t write(uint8_t byte) { return write(&byte, 1); }
    size_t write(const uint8_t* data, size_t length);
    bool seek(size_t position) {
        if (!valid_ || directory_ || position > size()) return false;
        position_ = position;
        return true;
    }
    void flush() {}
    size_t print(const String& text) {
        return write(reinterpret_cast<const uint8_t*>(text.c_str()), text.length());
    }
    String readString();
    void close() { valid_ = false; }
    File openNextFile();

private:
    FakeIndexedStorage* storage_ = nullptr;
    std::string path_;
    bool directory_ = false;
    bool valid_ = false;
    size_t position_ = 0;
    std::vector<std::string> children_;
    size_t next_ = 0;
};

class FakeIndexedStorage {
public:
    std::map<std::string, std::vector<uint8_t>> files;
    std::set<std::string> directories;
    std::map<std::string, unsigned> opens;
    std::string fail_open_path;
    std::string fail_remove_path;
    std::string short_read_path;
    bool fail_rename = false;
    void (*rename_hook)(const char*, const char*) = nullptr;
    void (*remove_hook)(const char*) = nullptr;
    void (*usage_hook)() = nullptr;
    void (*write_hook)(const char*, size_t) = nullptr;
    void (*exists_hook)(const char*) = nullptr;
    bool short_write = false;
    uint64_t capacity = 1024 * 1024;

    bool exists(const char* path) const {
        if (exists_hook) exists_hook(path);
        return files.count(path) || directories.count(path);
    }
    uint64_t totalBytes() const { return capacity; }
    uint64_t usedBytes() const {
        if (usage_hook) usage_hook();
        uint64_t used = 0;
        for (const auto& file : files) used += file.second.size();
        return used;
    }
    bool mkdir(const char* path) { directories.insert(path); return true; }
    bool rmdir(const String& path) { return directories.erase(path.c_str()) != 0; }
    bool remove(const char* path) {
        if (remove_hook) remove_hook(path);
        return path != fail_remove_path && files.erase(path) != 0;
    }
    bool remove(const String& path) { return remove(path.c_str()); }
    bool rename(const char* from, const char* to) {
        if (rename_hook) rename_hook(from, to);
        if (fail_rename || !files.count(from) || exists(to)) return false;
        files[to] = std::move(files[from]);
        files.erase(from);
        return true;
    }
    File open(const char* path, const char* mode = "r") {
        ++opens[path];
        if (path == fail_open_path) return File();
        if (*mode == 'w') files[path].clear();
        if (!exists(path)) return File();
        return File(this, path, directories.count(path) != 0);
    }
    File open(const String& path, const char* mode = "r") { return open(path.c_str(), mode); }
};

inline File::File(FakeIndexedStorage* storage, const std::string& path, bool directory)
    : storage_(storage), path_(path), directory_(directory), valid_(true) {
    if (directory_) {
        const std::string prefix = path_ + "/";
        for (const auto& entry : storage_->files) {
            if (entry.first.compare(0, prefix.size(), prefix) == 0 &&
                entry.first.find('/', prefix.size()) == std::string::npos) {
                children_.push_back(entry.first);
            }
        }
    }
}

inline size_t File::size() const { return valid_ ? storage_->files[path_].size() : 0; }
inline size_t File::read(uint8_t* data, size_t length) {
    if (!valid_ || directory_) return 0;
    const auto& bytes = storage_->files[path_];
    if (position_ >= bytes.size()) return 0;
    length = std::min(length, bytes.size() - position_);
    if (path_ == storage_->short_read_path && length) --length;
    memcpy(data, bytes.data() + position_, length);
    position_ += length;
    return length;
}
inline size_t File::write(const uint8_t* data, size_t length) {
    if (!valid_ || directory_) return 0;
    if (storage_->write_hook) storage_->write_hook(path_.c_str(), length);
    if (storage_->short_write && length) --length;
    auto& bytes = storage_->files[path_];
    if (position_ + length > bytes.size()) bytes.resize(position_ + length);
    memcpy(bytes.data() + position_, data, length);
    position_ += length;
    return length;
}
inline String File::readString() {
    const auto& bytes = storage_->files[path_];
    return std::string(bytes.begin() + position_, bytes.end());
}
inline File File::openNextFile() {
    return next_ < children_.size() ? storage_->open(children_[next_++].c_str()) : File();
}

extern FakeIndexedStorage LittleFS;
