#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

using String = std::string;
constexpr size_t RESPONSE_TRY_AGAIN = 0xffffffffu;
constexpr int RESPONSE_HEADERS = 1;

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
    virtual ~AsyncWebServerResponse() = default;
    virtual bool _sourceValid() const = 0;
    virtual void _respond(AsyncWebServerRequest*) = 0;
    virtual size_t _ack(AsyncWebServerRequest*, size_t, uint32_t) = 0;
    void setCode(int) {}
    void setContentType(const char*) {}
    void addHeader(const char*, const char*, bool = true) {}
    void _assembleHead(String& out, int) { out = "HTTP/1.1 200 OK\r\n\r\n"; }
protected:
    bool _sendContentLength = true;
    int _state = 0;
};

class AsyncWebServerRequest {
public:
    AsyncClient connection;
    AsyncWebServerResponse* response = nullptr;
    std::function<void()> disconnected;
    bool has_header = true;
    int code = 0;
    bool hasHeader(const char*) const { return has_header; }
    AsyncClient* client() { return &connection; }
    int version() const { return 1; }
    void onDisconnect(std::function<void()> callback) { disconnected = std::move(callback); }
    void send(int status, const char*, const char*) { code = status; }
    void send(AsyncWebServerResponse* value) { response = value; response->_respond(this); }
    void poll() { if (response) response->_ack(this, 0, 0); }
    void disconnect() { if (disconnected) disconnected(); delete response; response = nullptr; }
};
