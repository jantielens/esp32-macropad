#pragma once
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

class FakeFontStorage;
class File {
public:
    File() = default;
    File(FakeFontStorage* storage, const std::string& path, bool directory);
    explicit operator bool() const { return valid; }
    bool isDirectory() const { return directory; }
    const char* name() const { return path.c_str(); }
    size_t size() const;
    size_t write(const uint8_t* data, size_t length);
    size_t read(uint8_t* data, size_t length);
    bool seek(uint32_t offset) { position = offset; return valid; }
    void flush() {}
    void close() { valid = false; }
    File openNextFile();
private:
    FakeFontStorage* storage = nullptr;
    std::string path;
    bool directory = false;
    bool valid = false;
    size_t position = 0;
    std::vector<std::string> children;
    size_t next = 0;
};
class FakeFontStorage {
public:
    std::map<std::string, std::vector<uint8_t>> files;
    bool mounted = true;
    bool short_write = false;
    bool short_read = false;
    void (*read_hook)() = nullptr;
    bool rename_failure = false;
    bool exists(const char* path) const { return files.count(path) != 0; }
    bool mkdir(const char* path) { files[path] = {}; return true; }
    bool remove(const char* path) { return files.erase(path) != 0; }
    bool rename(const char* source, const char* destination) {
        if (rename_failure || !exists(source) || exists(destination)) return false;
        files[destination] = std::move(files[source]); files.erase(source); return true;
    }
    File open(const char* path, const char* mode = "r") {
        if (*mode == 'w') files[path].clear();
        return exists(path) ? File(this, path, strcmp(path, "/fonts") == 0) : File();
    }
};
inline File::File(FakeFontStorage* filesystem, const std::string& filename, bool is_directory)
    : storage(filesystem), path(filename), directory(is_directory), valid(true) {
    if (directory) for (const auto& item : storage->files) if (item.first != path) children.push_back(item.first);
}
inline size_t File::size() const { return valid ? storage->files[path].size() : 0; }
inline size_t File::write(const uint8_t* data, size_t length) {
    if (!valid) return 0;
    if (storage->short_write && length) --length;
    auto& bytes = storage->files[path];
    bytes.insert(bytes.end(), data, data + length); return length;
}
inline size_t File::read(uint8_t* data, size_t length) {
    if (!valid) return 0;
    if (storage->read_hook) storage->read_hook();
    if (storage->short_read && length) --length;
    const auto& bytes = storage->files[path];
    if (position > bytes.size()) return 0;
    length = std::min(length, bytes.size() - position);
    memcpy(data, bytes.data() + position, length); position += length; return length;
}
inline File File::openNextFile() {
    return next < children.size() ? storage->open(children[next++].c_str()) : File();
}
extern FakeFontStorage LittleFS;
extern FakeFontStorage SD_MMC;