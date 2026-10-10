#pragma once
#define WEB_PORTAL_JSON_H
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <memory>
inline std::shared_ptr<JsonDocument> make_psram_json_doc(size_t) { return std::make_shared<JsonDocument>(); }
inline void web_portal_send_json_error(AsyncWebServerRequest* request, int code, const char* message) {
    request->send(code, "application/json", message);
}
inline void web_portal_send_json_chunked(AsyncWebServerRequest* request, const std::shared_ptr<JsonDocument>& document) {
    std::string body;
    serializeJson(*document, body);
    request->send(200, "application/json", body.c_str());
}