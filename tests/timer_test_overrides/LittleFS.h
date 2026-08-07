#pragma once

#include <map>
#include <memory>
#include <string>

struct FakeFileState {
    std::string path;
    std::string content;
    size_t position = 0;
    bool writable = false;
    bool valid = false;
};

extern std::map<std::string, std::string> timer_test_files;
extern bool timer_test_fail_open;
extern size_t timer_test_write_limit;

class File {
public:
    File() : state(std::make_shared<FakeFileState>()) {}
    explicit File(const std::shared_ptr<FakeFileState>& value) : state(value) {}
    explicit operator bool() const { return state->valid; }
    size_t size() const { return state->content.size(); }
    int read() {
        if (state->position >= state->content.size()) return -1;
        return (unsigned char)state->content[state->position++];
    }
    int available() const { return (int)(state->content.size() - state->position); }
    size_t write(const uint8_t* data, size_t length) {
        if (!state->valid || !state->writable) return 0;
        size_t remaining = timer_test_write_limit > state->content.size()
            ? timer_test_write_limit - state->content.size() : 0;
        size_t count = length < remaining ? length : remaining;
        state->content.append((const char*)data, count);
        return count;
    }
    size_t write(uint8_t value) { return write(&value, 1); }
    void close() {
        if (state->valid && state->writable) timer_test_files[state->path] = state->content;
        state->valid = false;
    }
private:
    std::shared_ptr<FakeFileState> state;
};

class FakeLittleFS {
public:
    bool exists(const char* path) const { return timer_test_files.count(path) != 0; }
    File open(const char* path, const char* mode = "r") {
        if (timer_test_fail_open) return File();
        auto state = std::make_shared<FakeFileState>();
        state->path = path;
        state->writable = mode && mode[0] == 'w';
        if (state->writable) {
            state->valid = true;
        } else {
            auto found = timer_test_files.find(path);
            if (found != timer_test_files.end()) {
                state->valid = true;
                state->content = found->second;
            }
        }
        return File(state);
    }
};

extern FakeLittleFS LittleFS;
