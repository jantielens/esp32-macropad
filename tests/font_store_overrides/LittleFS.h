#pragma once
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

class String {
public:
    String() = default;
    String(const char* text) : value(text ? text : "") {}
    String& operator=(const char* text) { value = text ? text : ""; return *this; }
    const char* c_str() const { return value.c_str(); }
    size_t length() const { return value.size(); }
private:
    std::string value;
};

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
    int read() { uint8_t value; return read(&value, 1) == 1 ? value : -1; }
    size_t readBytes(char* data, size_t length) { return read(reinterpret_cast<uint8_t*>(data), length); }
    bool seek(uint32_t offset) { position = offset; return valid; }
    void flush() {}
    void close() { valid = false; }
    File openNextFile();
    String getNextFileName(bool* is_directory);
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
    std::set<std::string> directories;
    std::set<std::string> unsupported_entries;
    bool mounted = true;
    bool short_write = false;
    bool short_read = false;
    std::string short_read_path;
    void (*read_hook)() = nullptr;
    bool rename_failure = false;
    bool remove_failure = false;
    unsigned icon_directory_opens = 0;
    std::map<std::string, unsigned> open_calls;
    std::map<std::string, unsigned> read_calls;
    mutable std::map<std::string, unsigned> exists_calls;
    bool exists(const char* path) const { ++exists_calls[path]; return files.count(path) != 0; }
    bool mkdir(const char* path) { files[path] = {}; directories.insert(path); return true; }
    bool remove(const char* path) { return !remove_failure && files.erase(path) != 0; }
    bool rename(const char* source, const char* destination) {
        if (rename_failure || !exists(source) || exists(destination)) return false;
        files[destination] = std::move(files[source]); files.erase(source); return true;
    }
    File open(const char* path, const char* mode = "r") {
        ++open_calls[path];
        if (strcmp(path, "/icons") == 0) ++icon_directory_opens;
        if (*mode == 'w') files[path].clear();
        return exists(path) ? File(this, path, directories.count(path) ||
            strcmp(path, "/fonts") == 0 || strcmp(path, "/icons") == 0 ||
            strcmp(path, "/icon_sources") == 0) : File();
    }
};
inline File::File(FakeFontStorage* filesystem, const std::string& filename, bool is_directory)
    : storage(filesystem), path(filename), directory(is_directory), valid(true) {
    if (directory) for (const auto& item : storage->files)
        if (item.first.compare(0, path.size() + 1, path + "/") == 0 &&
            item.first.find('/', path.size() + 1) == std::string::npos) children.push_back(item.first);
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
    ++storage->read_calls[path];
    if (storage->read_hook) storage->read_hook();
    if ((storage->short_read || storage->short_read_path == path) && length) --length;
    const auto& bytes = storage->files[path];
    if (position > bytes.size()) return 0;
    length = std::min(length, bytes.size() - position);
    memcpy(data, bytes.data() + position, length); position += length; return length;
}
inline File File::openNextFile() {
    while (next < children.size() && storage->unsupported_entries.count(children[next])) ++next;
    return next < children.size() ? storage->open(children[next++].c_str()) : File();
}
inline String File::getNextFileName(bool* is_directory) {
    if (next >= children.size()) return "";
    const std::string& child = children[next++];
    if (storage->unsupported_entries.count(child)) return "";
    if (is_directory) *is_directory = storage->directories.count(child) != 0;
    return child.c_str();
}
extern FakeFontStorage LittleFS;
extern FakeFontStorage SD_MMC;