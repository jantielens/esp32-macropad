#pragma once

#include <cstring>
#include <cstdint>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

class Preferences {
public:
    bool write_succeeds = true;
    std::map<std::string, std::vector<uint8_t>> values;

    bool isKey(const char* key) const { return values.count(key) != 0; }
    size_t getBytesLength(const char* key) const {
        const auto entry = values.find(key);
        return entry == values.end() ? 0 : entry->second.size();
    }
    size_t getBytes(const char* key, void* data, size_t capacity) const {
        const auto entry = values.find(key);
        if (entry == values.end() || entry->second.size() > capacity) return 0;
        std::memcpy(data, entry->second.data(), entry->second.size());
        return entry->second.size();
    }
    size_t putBytes(const char* key, const void* data, size_t length) {
        if (!write_succeeds) return 0;
        const auto* bytes = static_cast<const uint8_t*>(data);
        values[key] = {bytes, bytes + length};
        return length;
    }
};
