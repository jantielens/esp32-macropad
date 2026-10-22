// ============================================================================
// Test stub: ESPAsyncWebServer.h — minimal shim for host-native compilation
// ============================================================================
// Provides just enough types so component_registry.h compiles on the host
// without pulling in the real ESP32 SDK or AsyncTCP.

#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>
#include <string>
#include <map>

class AsyncWebParameter {
public:
    std::string text;
    const std::string& value() const { return text; }
};

class AsyncWebServerRequest {
public:
    void* _tempObject = nullptr;
    bool isSent() const { return response_code != 0; }
    int response_code = 0;
    std::string response_body;
    bool authorized = true;
    size_t body_length = 0;
    std::map<std::string, AsyncWebParameter> parameters;
    std::function<void()> disconnect_handler;
    bool hasParam(const char* name) const { return parameters.count(name) != 0; }
    AsyncWebParameter* getParam(const char* name) { return &parameters[name]; }
    size_t contentLength() const { return body_length; }
    void onDisconnect(std::function<void()> handler) { disconnect_handler = std::move(handler); }
    void disconnect() { if (disconnect_handler) disconnect_handler(); }

    void send(int code, const char* = nullptr, const char* body = nullptr) {
        response_code = code;
        response_body = body ? body : "";
    }
};
class AsyncWebServerResponse {};

typedef std::function<void(AsyncWebServerRequest*)> ArRequestHandlerFunction;
typedef std::function<void(AsyncWebServerRequest*, uint8_t*, size_t, size_t, size_t)> ArBodyHandlerFunction;
typedef int WebRequestMethodComposite;

#define HTTP_GET     0b00000001
#define HTTP_POST    0b00000010
#define HTTP_DELETE  0b00000100
#define HTTP_PUT     0b00001000
#define HTTP_OPTIONS 0b01000000
