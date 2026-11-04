#pragma once

#include <Arduino.h>
#include <freertos/task.h>
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

using UBaseType_t = unsigned;
using StackType_t = uint8_t;
using TaskFunction_t = void (*)(void*);
constexpr int HTTPC_STRICT_FOLLOW_REDIRECTS = 1;
constexpr int WL_CONNECTED = 3;

struct FetchResponse {
    int status = 200;
    std::string body = "image";
    int length = 5;
    std::string content_type = "image/jpeg";
};

inline FetchResponse fetch_response;
inline std::vector<std::string> fetch_requests;
inline std::vector<std::string> fetch_authorizations;
inline unsigned fetch_live_clients = 0;
inline std::function<void()> fetch_on_get;
inline std::function<void()> fetch_on_byte_read;
inline std::function<void()> fetch_on_read;
inline std::function<void()> fetch_on_decode;
inline uint32_t fetch_now = 0;
inline bool fetch_decoded = false;
inline bool fetch_ota = false;
inline unsigned fetch_delays = 0;
inline unsigned fetch_decodes = 0;
inline bool fetch_decode_ok = true;
inline TaskFunction_t fetch_worker = nullptr;

struct FetchStepComplete {};
inline void vTaskDelay(uint32_t ticks) {
    fetch_now += ticks;
    if (ticks == 500 || (fetch_decoded && (ticks == 5 || ticks == 10))) throw FetchStepComplete{};
    if (++fetch_delays > 10000) throw FetchStepComplete{};
}
inline unsigned uxTaskGetStackHighWaterMark(TaskHandle_t) { return 4096; }
inline size_t strlcpy(char* target, const char* source, size_t size) {
    const size_t length = strlen(source);
    if (size) {
        const size_t copied = std::min(length, size - 1);
        memcpy(target, source, copied);
        target[copied] = '\0';
    }
    return length;
}

class String {
    std::string value;
public:
    String(std::string text = {}) : value(std::move(text)) {}
    bool isEmpty() const { return value.empty(); }
    int indexOf(const char* part) const {
        auto index = value.find(part);
        return index == std::string::npos ? -1 : static_cast<int>(index);
    }
    const char* c_str() const { return value.c_str(); }
    String substring(size_t begin) const { return value.substr(begin); }
    String substring(size_t begin, size_t end) const { return value.substr(begin, end - begin); }
    size_t length() const { return value.size(); }
    void trim() {}
    bool startsWith(const char* text) const { return value.rfind(text, 0) == 0; }
    bool endsWith(const char* text) const {
        const size_t length = strlen(text);
        return value.size() >= length && value.compare(value.size() - length, length, text) == 0;
    }
};

class WiFiClient {
public:
    std::string body;
    size_t position = 0;
    bool open = true;
    WiFiClient() { ++fetch_live_clients; }
    virtual ~WiFiClient() { --fetch_live_clients; }
    virtual int read() {
        if (fetch_on_byte_read) {
            auto hook = std::move(fetch_on_byte_read);
            fetch_on_byte_read = nullptr;
            hook();
        }
        return position < body.size() ? static_cast<unsigned char>(body[position++]) : -1;
    }
    virtual int read(uint8_t* target, size_t size) { return readBytes(target, size); }
    size_t readBytes(uint8_t* target, size_t size) {
        if (fetch_on_read) {
            auto hook = std::move(fetch_on_read);
            fetch_on_read = nullptr;
            hook();
        }
        const size_t count = std::min(size, body.size() - position);
        memcpy(target, body.data() + position, count);
        position += count;
        return count;
    }
    size_t available() const { return body.size() - position; }
    bool connected() const { return open && available(); }
    void stop() { open = false; }
    void setTimeout(unsigned) {}
};

class WiFiClientSecure : public WiFiClient {
public:
    void setInsecure() {}
};

class HTTPClient {
    WiFiClient* client = nullptr;
    std::string url;
    std::string authorization;
public:
    void setReuse(bool) {}
    void setFollowRedirects(int) {}
    bool begin(WiFiClient& connection, const char* source) {
        client = &connection;
        url = source;
        return true;
    }
    void setTimeout(unsigned) {}
    void collectHeaders(const char**, size_t) {}
    void setAuthorization(const char* user, const char* password) {
        authorization = std::string(user) + ":" + password;
    }
    void setAuthorization(const char* value) { authorization = value; }
    int GET() {
        fetch_requests.push_back(url);
        fetch_authorizations.push_back(authorization);
        client->body = fetch_response.body;
        client->position = 0;
        client->open = true;
        if (fetch_on_get) {
            auto hook = std::move(fetch_on_get);
            fetch_on_get = nullptr;
            hook();
        }
        return fetch_response.status;
    }
    String header(const char*) const { return fetch_response.content_type; }
    int getSize() const { return fetch_response.length; }
    WiFiClient* getStreamPtr() { return client; }
    bool connected() const { return client && client->connected(); }
    void end() {}
};

struct FetchWiFi {
    int status() const { return WL_CONNECTED; }
};
inline FetchWiFi WiFi;