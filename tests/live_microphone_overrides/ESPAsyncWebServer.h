#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

using String = std::string;
constexpr size_t RESPONSE_TRY_AGAIN = 0xffffffffu;
constexpr int RESPONSE_HEADERS = 1;
using WebRequestMethodComposite = int;
constexpr int HTTP_GET = 1;
constexpr int HTTP_PUT = 8;

class AsyncClient {
public:
    std::string output;
    bool closed = false;
    bool canSend() const { return !closed; }
    size_t space() const { return 1460; }
    size_t add(const char* data, size_t size) { output.append(data, size); return size; }
    bool send() { return true; }
    void close() { closed = true; }
};

class AsyncWebServerRequest;
class AsyncWebServerResponse {
public:
    int code = 200;
    std::string body;
    virtual ~AsyncWebServerResponse() = default;
    virtual bool _sourceValid() const { return true; }
    virtual void _respond(AsyncWebServerRequest*) {}
    virtual size_t _ack(AsyncWebServerRequest*, size_t, uint32_t) { return 0; }
    void setCode(int value) { code = value; }
    void setContentType(const char*) {}
    void addHeader(const char*, const char*, bool = true) {}
    void _assembleHead(String& out, int) { out = "HTTP/1.1 200 OK\r\n\r\n"; }
protected:
    bool _sendContentLength = true;
    int _state = 0;
};

class AsyncWebServerRequest {
public:
    void* _tempObject = nullptr;
    AsyncClient connection;
    AsyncWebServerResponse* response = nullptr;
    std::function<void()> disconnected;
    bool has_header = true;
    int code = 0;
    std::string body;
    bool isSent() const { return code != 0; }
    bool hasHeader(const char*) const { return has_header; }
    AsyncClient* client() { return &connection; }
    int version() const { return 1; }
    void onDisconnect(std::function<void()> callback) { disconnected = std::move(callback); }
    AsyncWebServerResponse* beginResponse(int status, const char*, const char* text) {
        auto* value = new AsyncWebServerResponse;
        value->code = status;
        value->body = text;
        return value;
    }
    void send(int status, const char*, const char* text) { code = status; body = text; }
    void send(AsyncWebServerResponse* value) {
        response = value;
        code = value->code;
        body = value->body;
        response->_respond(this);
    }
    void poll() { if (response) response->_ack(this, 0, 0); }
    void disconnect() { if (disconnected) disconnected(); delete response; response = nullptr; }
};

using ArRequestHandlerFunction = std::function<void(AsyncWebServerRequest*)>;
using ArBodyHandlerFunction =
    std::function<void(AsyncWebServerRequest*, uint8_t*, size_t, size_t, size_t)>;
