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

class AsyncWebServerRequest {
public:
    void* _tempObject = nullptr;
    int response_code = 0;
    std::string response_body;

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
