#pragma once

#include <algorithm>
#include <map>
#include <string>
#include <vector>

class FakeStorage;

class File {
public:
    File() = default;
    File(FakeStorage* storage, std::string path, bool directory);

    explicit operator bool() const { return valid_; }
    bool isDirectory() const { return directory_; }
    const char* name() const { return path_.c_str(); }
    File openNextFile();
    void close() { valid_ = false; }

private:
    FakeStorage* storage_ = nullptr;
    std::string path_;
    bool directory_ = false;
    bool valid_ = false;
    std::vector<std::string> children_;
    size_t next_child_ = 0;
};

class FakeStorage {
public:
    struct Node { bool directory; };

    bool exists(const char* path) const { return nodes_.count(path ? path : "") != 0; }

    File open(const char* path, const char* = "r") {
        const std::string value(path ? path : "");
        const auto found = nodes_.find(value);
        return found == nodes_.end() ? File() : File(this, value, found->second.directory);
    }

    bool remove(const char* path) {
        const std::string value(path ? path : "");
        if (value == fail_remove_path) return false;
        const auto found = nodes_.find(value);
        if (found == nodes_.end() || found->second.directory) return false;
        nodes_.erase(found);
        return true;
    }

    bool rmdir(const char* path) {
        const std::string value(path ? path : "");
        if (value == fail_rmdir_path) return false;
        const auto found = nodes_.find(value);
        if (found == nodes_.end() || !found->second.directory || !children(value).empty()) return false;
        nodes_.erase(found);
        return true;
    }

    void add_directory(const std::string& path) { nodes_[path] = {true}; }
    void add_file(const std::string& path) { nodes_[path] = {false}; }
    void clear() {
        nodes_.clear();
        fail_remove_path.clear();
        fail_rmdir_path.clear();
    }

    std::vector<std::string> children(const std::string& path) const {
        const std::string prefix = path == "/" ? "/" : path + "/";
        std::vector<std::string> result;
        for (const auto& entry : nodes_) {
            if (entry.first.rfind(prefix, 0) != 0) continue;
            const std::string remainder = entry.first.substr(prefix.size());
            if (!remainder.empty() && remainder.find('/') == std::string::npos) result.push_back(entry.first);
        }
        return result;
    }

    std::string fail_remove_path;
    std::string fail_rmdir_path;

private:
    std::map<std::string, Node> nodes_;
};

inline File::File(FakeStorage* storage, std::string path, bool directory)
    : storage_(storage), path_(std::move(path)), directory_(directory), valid_(true) {
    if (directory_) children_ = storage_->children(path_);
}

inline File File::openNextFile() {
    if (!valid_ || !directory_ || next_child_ >= children_.size()) return File();
    const std::string& child = children_[next_child_++];
    File result = storage_->open(child.c_str());
    return result;
}

extern FakeStorage SD_MMC;