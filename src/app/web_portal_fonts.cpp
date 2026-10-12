#include "web_portal_fonts.h"
#if HAS_TTF_FONTS
#include "font_store.h"
#include "log_manager.h"
#include "web_portal_auth.h"
#include "web_portal_json.h"
#include <new>

struct FontRequestState {
    FontUpload upload;
    FontStoreResult result = FontStoreResult::Invalid;
    size_t total = 0;
    size_t next = 0;
};

static void font_api_result(AsyncWebServerRequest* request, FontStoreResult result) {
    switch (result) {
        case FontStoreResult::Ok: request->send(200, "application/json", "{\"status\":\"ok\"}"); return;
        case FontStoreResult::Invalid: LOGW("Fonts", "API 400: invalid ID, upload framing, or TrueType file"); web_portal_send_json_error(request, 400, "Invalid font ID or unsupported static TrueType file"); return;
        case FontStoreResult::NotFound: LOGD("Fonts", "API 404: font not found"); web_portal_send_json_error(request, 404, "Font not found"); return;
        case FontStoreResult::Duplicate: LOGD("Fonts", "API 409: duplicate font ID"); web_portal_send_json_error(request, 409, "Font ID already exists; delete it before replacing"); return;
        case FontStoreResult::Busy: LOGD("Fonts", "API 409: font operation is in progress"); web_portal_send_json_error(request, 409, "A font upload or load is in progress; retry shortly"); return;
        case FontStoreResult::Unavailable: LOGW("Fonts", "API 503: runtime PSRAM unavailable"); web_portal_send_json_error(request, 503, "Runtime PSRAM is unavailable"); return;
        case FontStoreResult::IoError: LOGW("Fonts", "API 507: storage failure or incomplete upload"); web_portal_send_json_error(request, 507, "Font storage operation failed or upload was incomplete"); return;
    }
}

static void font_request_cleanup(AsyncWebServerRequest* request) {
    auto* state = static_cast<FontRequestState*>(request->_tempObject);
    if (!state) return;
    request->_tempObject = nullptr;
    font_store_abort(state->upload);
    delete state;
}

void handleFontUploadBody(AsyncWebServerRequest* request, uint8_t* data, size_t length, size_t index, size_t total) {
    if (!portal_auth_gate(request)) return;
    if (index == 0 && !request->_tempObject) {
        auto* state = new (std::nothrow) FontRequestState;
        if (!state) return;
        request->_tempObject = state;
        request->onDisconnect([request]() { font_request_cleanup(request); });
        state->total = total;
        if (total && request->hasParam("name"))
            state->result = font_store_begin(state->upload, request->getParam("name")->value().c_str());
    }
    auto* state = static_cast<FontRequestState*>(request->_tempObject);
    if (!state || state->result != FontStoreResult::Ok) return;
    if (total != state->total || index != state->next || index > total || length > total - index) {
        state->result = FontStoreResult::Invalid;
        font_store_abort(state->upload);
        return;
    }
    state->result = font_store_write(state->upload, data, length);
    if (state->result == FontStoreResult::Ok) state->next += length;
}

void handleFontUploadComplete(AsyncWebServerRequest* request) {
    if (!portal_auth_gate(request)) return;
    auto* state = static_cast<FontRequestState*>(request->_tempObject);
    FontStoreResult result = state ? state->result :
        (request->contentLength() ? FontStoreResult::IoError : FontStoreResult::Invalid);
    if (state && result == FontStoreResult::Ok) {
        result = state->next == state->total ? font_store_finish(state->upload, state->total) : FontStoreResult::IoError;
    }
    font_request_cleanup(request);
    font_api_result(request, result);
}

void handleGetFonts(AsyncWebServerRequest* request) {
    if (!portal_auth_gate(request)) return;
    auto document = make_psram_json_doc(1024);
    JsonArray fonts = document->to<JsonArray>();
    const FontStoreResult result = font_store_list([](const char* name, size_t size, void* context) -> bool {
        auto* array = static_cast<JsonArray*>(context);
        JsonObject font = array->add<JsonObject>();
        font["name"] = JsonString(name, JsonString::Copied);
        font["size_bytes"] = size;
        return !font.isNull();
    }, &fonts);
    if (result != FontStoreResult::Ok) { font_api_result(request, result); return; }
    if (document->overflowed()) { LOGW("Fonts", "API 507: font-list allocation failed"); web_portal_send_json_error(request, 507, "Unable to allocate font list"); return; }
    web_portal_send_json_chunked(request, document);
}

void handleDeleteFont(AsyncWebServerRequest* request) {
    if (!portal_auth_gate(request)) return;
    font_api_result(request, request->hasParam("name") ?
        font_store_delete(request->getParam("name")->value().c_str()) : FontStoreResult::Invalid);
}
#endif