#include "font_store.h"
extern "C" unsigned long millis() { return 0; }
size_t heap_caps_get_free_size(uint32_t) { return 8192; }
size_t heap_caps_get_largest_free_block(uint32_t) { return 4096; }
#include "web_portal_fonts.h"
#include <cassert>
#include <cstdlib>
#include <cstring>

FakeFontStorage LittleFS;
FakeFontStorage SD_MMC;
bool storage_mount() { return true; }
void storage_publish_usage(bool) {}
bool ota_activity_is_active() { return false; }
static bool psram_available = true;
size_t heap_caps_get_total_size(uint32_t) { return psram_available ? 1024 * 1024 : 0; }
void* heap_caps_malloc(size_t size, uint32_t) { return malloc(size); }
void heap_caps_free(void* pointer) { free(pointer); }
bool portal_auth_gate(AsyncWebServerRequest* request) {
    if (!request->authorized) request->send(401);
    return request->authorized;
}
static uint8_t fixture[] = {
#include "fixtures/BebasNeue.inc"
};
static AsyncWebServerRequest upload_request(const char* name) {
    AsyncWebServerRequest request;
    request.parameters["name"].text = name;
    request.body_length = sizeof(fixture);
    return request;
}
int main() {
    const uint32_t initial_generation = font_store_generation();
    auto first = upload_request("first");
    auto second = upload_request("second");
    const size_t split = sizeof(fixture) / 2;
    handleFontUploadBody(&first, fixture, split, 0, sizeof(fixture));
    handleFontUploadBody(&second, fixture, sizeof(fixture), 0, sizeof(fixture));
    handleFontUploadComplete(&second);
    assert(second.response_code == 409 && second._tempObject == nullptr);
    handleFontUploadBody(&first, fixture + split, sizeof(fixture) - split, split, sizeof(fixture));
    assert(!Storage.exists("/fonts/first.ttf"));
    handleFontUploadComplete(&first);
    assert(first.response_code == 200 && first._tempObject == nullptr && Storage.exists("/fonts/first.ttf"));
    assert(font_store_generation() == initial_generation + 1);
    auto duplicate = upload_request("first");
    handleFontUploadBody(&duplicate, fixture, sizeof(fixture), 0, sizeof(fixture));
    handleFontUploadComplete(&duplicate);
    assert(duplicate.response_code == 409 && duplicate._tempObject == nullptr);
    assert(font_store_generation() == initial_generation + 1);
    first.disconnect();
    auto interrupted = upload_request("interrupted");
    handleFontUploadBody(&interrupted, fixture, split, 0, sizeof(fixture));
    interrupted.disconnect();
    assert(!interrupted._tempObject && !Storage.exists("/fonts/.upload.tmp"));
    auto incomplete = upload_request("incomplete");
    handleFontUploadBody(&incomplete, fixture, split, 0, sizeof(fixture));
    handleFontUploadComplete(&incomplete);
    assert(incomplete.response_code == 507 && !Storage.exists("/fonts/incomplete.ttf"));
    auto reordered = upload_request("reordered");
    handleFontUploadBody(&reordered, fixture, split, 0, sizeof(fixture));
    handleFontUploadBody(&reordered, fixture + split, sizeof(fixture) - split, split + 1, sizeof(fixture));
    handleFontUploadComplete(&reordered);
    assert(reordered.response_code == 400 && !Storage.exists("/fonts/.upload.tmp"));
    auto invalid = upload_request("invalid");
    uint8_t unsupported[] = {'O', 'T', 'T', 'O'};
    handleFontUploadBody(&invalid, unsupported, sizeof(unsupported), 0, sizeof(unsupported));
    handleFontUploadComplete(&invalid);
    assert(invalid.response_code == 400 && !Storage.exists("/fonts/invalid.ttf"));
    auto next = upload_request("different-name");
    handleFontUploadBody(&next, fixture, sizeof(fixture), 0, sizeof(fixture));
    handleFontUploadComplete(&next);
    assert(next.response_code == 200);
    AsyncWebServerRequest list;
    handleGetFonts(&list);
    JsonDocument fonts;
    assert(!deserializeJson(fonts, list.response_body));
    assert(fonts.size() == 2);
    assert(strcmp(fonts[0]["name"], "different-name") == 0 && strcmp(fonts[1]["name"], "first") == 0);
    auto unauthorized = upload_request("denied");
    unauthorized.authorized = false;
    handleFontUploadBody(&unauthorized, fixture, sizeof(fixture), 0, sizeof(fixture));
    handleFontUploadComplete(&unauthorized);
    assert(unauthorized.response_code == 401 && unauthorized._tempObject == nullptr);
    handleDeleteFont(&unauthorized);
    assert(unauthorized.response_code == 401);
    psram_available = false;
    auto unavailable = upload_request("unavailable");
    handleFontUploadBody(&unavailable, fixture, sizeof(fixture), 0, sizeof(fixture));
    handleFontUploadComplete(&unavailable);
    assert(unavailable.response_code == 503 && unavailable._tempObject == nullptr);
    handleGetFonts(&unavailable);
    assert(unavailable.response_code == 503);
    psram_available = true;
    auto remove = upload_request("first");
    handleDeleteFont(&remove);
    assert(remove.response_code == 200);
    assert(font_store_generation() == initial_generation + 3);
    handleDeleteFont(&remove);
    assert(remove.response_code == 404);
    assert(font_store_generation() == initial_generation + 3);
}