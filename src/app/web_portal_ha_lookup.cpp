#include "board_config.h"
#if HAS_HA_LIVE
#include "web_portal_device_api.h"
#include "web_portal_auth.h"
#include "ha_lookup.h"
#include "log_manager.h"
#include <algorithm>
#include <esp_heap_caps.h>
#include <errno.h>
#include <new>
#include <stdlib.h>

class HaLookupResponse final : public AsyncAbstractResponse {
public:
    explicit HaLookupResponse(char* body) : body_(body) {
        _code = 200;
        _contentType = "application/json";
        _contentLength = strlen(body);
        _sendContentLength = true;
        _chunked = false;
        addHeader("Cache-Control", "no-store");
    }
    ~HaLookupResponse() override { heap_caps_free(body_); }
    bool _sourceValid() const override { return body_ != nullptr; }
    size_t _fillBuffer(uint8_t* buffer, size_t max_length) override {
        const size_t count = std::min(max_length, _contentLength - offset_);
        memcpy(buffer, body_ + offset_, count);
        offset_ += count;
        return count;
    }
private:
    char* body_;
    size_t offset_ = 0;
};

void handleGetHaLookup(AsyncWebServerRequest* request) {
    if (!portal_auth_gate(request)) return;
    HaLookupResult result;
    uint32_t id = 0;
    const bool polling = request->hasParam("job");
    if (polling) {
        const String value = request->getParam("job")->value();
        char* end = nullptr;
        errno = 0;
        unsigned long parsed = strtoul(value.c_str(), &end, 10);
        if (value.isEmpty() || value.length() > 10 || value[0] < '1' || value[0] > '9' ||
            !end || *end || errno == ERANGE || parsed > UINT32_MAX) {
            request->send(400, "application/json", "{\"error\":\"Invalid lookup job ID.\"}");
            return;
        }
        id = uint32_t(parsed);
        result = ha_lookup_poll(id, nullptr, 0);
    } else if (request->hasParam("entity") != request->hasParam("query")) {
        const bool entity = request->hasParam("entity");
        result = ha_lookup_start(request->getParam(entity ? "entity" : "query")->value().c_str(), entity);
    } else {
        result = {400, 0, "Supply either query or entity."};
    }
    if (polling && result.status == 200) {
        char* body = static_cast<char*>(heap_caps_malloc(HA_LOOKUP_RESPONSE_BYTES + 1,
                                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!body) result = {503, id, "HA lookup response PSRAM allocation failed."};
        else {
            result = ha_lookup_poll(id, body, HA_LOOKUP_RESPONSE_BYTES + 1);
            if (result.status == 200) {
                auto* response = new (std::nothrow) HaLookupResponse(body);
                if (response) { request->send(response); return; }
                result = {503, id, "HA lookup response allocation failed."};
            }
            heap_caps_free(body);
        }
    }
    if (result.error) LOGW("HALookup", "%s", result.error);
    String body = result.error ? String("{\"error\":\"") + result.error + "\"}"
                               : String("{\"job\":") + result.id + "}";
    request->send(result.status, "application/json", body);
}
#endif
