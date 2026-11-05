#ifndef MCP_TOOL_RESPONSE_H
#define MCP_TOOL_RESPONSE_H

#include <ArduinoJson.h>
#include <stdint.h>
#include <memory>

template <typename TDoc>
std::shared_ptr<TDoc> mcp_make_tool_response(
    const std::shared_ptr<TDoc>& doc, JsonVariantConst id, JsonVariantConst value,
    void* (*allocate)(size_t), void (*release)(void*)) {
    const size_t length = measureJson(value);
    if (length == SIZE_MAX) return {};
    char* raw = static_cast<char*>(allocate(length + 1));
    if (!raw) return {};
    std::shared_ptr<char> text(raw, release);
    if (serializeJson(value, raw, length + 1) != length) return {};

    JsonObject root = doc->template to<JsonObject>();
    root["jsonrpc"] = "2.0";
    root["id"] = id;
    JsonObject result = root["result"].to<JsonObject>();
    JsonObject item = result["content"].to<JsonArray>().add<JsonObject>();
    item["type"] = "text";
    item["text"] = JsonString(raw, JsonString::Linked);
    result["isError"] = false;
    if (doc->overflowed()) return {};

    // The async response keeps the document and its linked text alive together.
    return std::shared_ptr<TDoc>(doc.get(), [doc, text](TDoc*) {});
}

#endif
