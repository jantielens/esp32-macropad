#pragma once
#include <functional>
#include <string>
struct AsyncWebServerRequest {
    int status = 0;
    std::string location;
    void redirect(const char* url) { status = 302; location = url; }
    void send(int code, const char*, const char*) { status = code; }
};
struct AsyncWebServer {
    std::function<void(AsyncWebServerRequest*)> not_found;
    void onNotFound(std::function<void(AsyncWebServerRequest*)> handler) { not_found = handler; }
};
