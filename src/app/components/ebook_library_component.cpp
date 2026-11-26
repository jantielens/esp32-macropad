#include "component_registry.h"

#if HAS_EREADER
#include "ereader_library.h"
#include "web_portal_json.h"
#include <new>

namespace {
struct EbookRequest {
    EreaderUpload upload;
    EreaderFileResult result = EreaderFileResult::Invalid;
    size_t total = 0;
    size_t next = 0;
};

void ebook_result(AsyncWebServerRequest* request, EreaderFileResult result) {
    switch (result) {
        case EreaderFileResult::Ok:
            request->send(200, "application/json", "{\"status\":\"ok\"}"); return;
        case EreaderFileResult::Invalid:
            web_portal_send_json_error(request, 400, "Invalid E-book filename or upload framing"); return;
        case EreaderFileResult::NotFound:
            web_portal_send_json_error(request, 404, "E-book not found"); return;
        case EreaderFileResult::Conflict:
            web_portal_send_json_error(request, 409, "E-book already exists or library is busy"); return;
        case EreaderFileResult::Unavailable:
            web_portal_send_json_error(request, 503, "E-book library unavailable"); return;
        case EreaderFileResult::StorageError:
            web_portal_send_json_error(request, 507, "E-book storage operation failed or upload was incomplete"); return;
    }
}

void ebook_cleanup(AsyncWebServerRequest* request) {
    auto* state = static_cast<EbookRequest*>(request->_tempObject);
    if (!state) return;
    request->_tempObject = nullptr;
    ereader_upload_abort(state->upload);
    delete state;
}

void ebook_upload_body(AsyncWebServerRequest* request, uint8_t* data, size_t length, size_t index, size_t total) {
    if (index == 0 && !request->_tempObject) {
        auto* state = new (std::nothrow) EbookRequest;
        if (!state) return;
        request->_tempObject = state;
        request->onDisconnect([request]() { ebook_cleanup(request); });
        state->total = total;
        if (total && request->contentLength() == total &&
            request->contentType() == "application/octet-stream" && request->hasParam("book"))
            state->result = ereader_upload_begin(state->upload, request->getParam("book")->value().c_str());
    }
    auto* state = static_cast<EbookRequest*>(request->_tempObject);
    if (!state || state->result != EreaderFileResult::Ok) return;
    if (total != state->total || index != state->next || index > total || length > total - index) {
        state->result = EreaderFileResult::Invalid;
        ereader_upload_abort(state->upload);
        return;
    }
    state->result = ereader_upload_write(state->upload, data, length);
    if (state->result == EreaderFileResult::Ok) state->next += length;
}

void ebook_upload_complete(AsyncWebServerRequest* request) {
    auto* state = static_cast<EbookRequest*>(request->_tempObject);
    EreaderFileResult result = state ? state->result :
        (request->contentLength() ? EreaderFileResult::Unavailable : EreaderFileResult::Invalid);
    if (state && result == EreaderFileResult::Ok)
        result = state->next == state->total ? ereader_upload_finish(state->upload, state->total)
                                             : EreaderFileResult::StorageError;
    ebook_cleanup(request);
    ebook_result(request, result);
}

void ebook_delete(AsyncWebServerRequest* request) {
    ebook_result(request, request->hasParam("book") ?
        ereader_library_delete(request->getParam("book")->value().c_str()) : EreaderFileResult::Invalid);
}

void ebook_list(AsyncWebServerRequest* request) {
    auto doc = make_psram_json_doc(65536);
    if (!doc || !doc->capacity()) {
        ebook_result(request, EreaderFileResult::Unavailable);
        return;
    }
    JsonArray books = (*doc)["books"].to<JsonArray>();
    const EreaderFileResult result = ereader_library_list([](const char* name, size_t size, void* context) {
        auto& array = *static_cast<JsonArray*>(context);
        JsonObject book = array.add<JsonObject>();
        book["id"] = JsonString(name, JsonString::Copied);
        book["title"] = JsonString(name, JsonString::Copied);
        book["size_bytes"] = size;
        return !book.isNull();
    }, &books);
    if (result != EreaderFileResult::Ok) { ebook_result(request, result); return; }
    if (doc->overflowed()) { ebook_result(request, EreaderFileResult::Unavailable); return; }
    web_portal_send_json_chunked(request, doc);
}

const ComponentAction ebook_actions[] = {
    {"list", HTTP_GET, ebook_list, nullptr},
    {"upload", HTTP_POST, ebook_upload_complete, ebook_upload_body},
    {"delete", HTTP_DELETE, ebook_delete, nullptr},
};
}

static ComponentDef ebook_library_component = {
    "ebook-library", "device", "E-book Library", 50,
    nullptr, nullptr, nullptr, nullptr, ebook_actions, 3,
    "ebook-library", nullptr, nullptr
};
REGISTER_COMPONENT(ebook_library)
#endif
