#include "board_config.h"

#if IS_SHUTTER_TESTER

#include "../shutter_test_scripts.h"
#include "web_portal_auth.h"
#include "web_portal_routes.h"
#include "component_registry.h"
#include "log_manager.h"

#include "storage.h"
#include "psram_json_allocator.h"
#include <ArduinoJson.h>
#include <string.h>

#define TAG "TestScriptsAPI"

// ============================================================================
// Component registration
// ============================================================================

REGISTER_NAV_COMPONENT(shutter_tests, "shutter-tests", "camera", "Guided Test Definitions", 11, "shutter-tests")

// ============================================================================
// GET /api/shutter/tests — return raw file content
// ============================================================================

static void handleGetTests(AsyncWebServerRequest* request) {
    if (!portal_auth_gate(request)) return;

    File f = Storage.open(SHUTTER_TEST_FILE_PATH, "r");
    if (!f) {
        // No file yet — return empty string (not an error)
        request->send(200, "text/plain", "");
        return;
    }

    size_t sz = f.size();
    if (sz == 0) {
        f.close();
        request->send(200, "text/plain", "");
        return;
    }

    // Read into PSRAM buffer for response
    char* buf = (char*)heap_caps_malloc(sz + 1, MALLOC_CAP_SPIRAM);
    if (!buf) {
        f.close();
        request->send(500, "application/json", "{\"success\":false,\"message\":\"Out of memory\"}");
        return;
    }

    f.readBytes(buf, sz);
    buf[sz] = '\0';
    f.close();

    request->send(200, "text/plain", buf);
    heap_caps_free(buf);
}

// ============================================================================
// PUT /api/shutter/tests — write raw file content
// ============================================================================

struct ShutterTestsUpload {
    size_t total;
    size_t received;
    bool invalid;
    uint8_t data[1];
};

static void shutter_tests_upload_reset(AsyncWebServerRequest* request) {
    auto* upload = static_cast<ShutterTestsUpload*>(request->_tempObject);
    request->_tempObject = nullptr;
    heap_caps_free(upload);
}

static void handlePutTestsBody(AsyncWebServerRequest* request,
                                uint8_t* data, size_t len,
                                size_t index, size_t total) {
    if (index == 0) {
        if (!portal_auth_gate(request)) return;
        if (total > SHUTTER_TEST_FILE_MAX_BYTES) {
            request->send(413, "application/json",
                          "{\"success\":false,\"message\":\"File too large\"}");
            return;
        }
        auto* upload = static_cast<ShutterTestsUpload*>(heap_caps_malloc(
            sizeof(ShutterTestsUpload) + total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!upload) {
            request->send(500, "application/json",
                          "{\"success\":false,\"message\":\"Out of memory\"}");
            return;
        }
        upload->total = total;
        upload->received = 0;
        upload->invalid = false;
        request->_tempObject = upload;
        request->onDisconnect([request]() { shutter_tests_upload_reset(request); });
    }

    auto* upload = static_cast<ShutterTestsUpload*>(request->_tempObject);
    if (!upload || upload->invalid) return;
    if (total != upload->total || index != upload->received ||
        len > upload->total - upload->received || (len && !data)) {
        upload->invalid = true;
        return;
    }
    if (len) memcpy(upload->data + index, data, len);
    upload->received += len;
}

static void handlePutTests(AsyncWebServerRequest* request) {
    if (!portal_auth_gate(request)) return;

    auto* upload = static_cast<ShutterTestsUpload*>(request->_tempObject);
    if (!upload && request->contentLength()) return; // body handler already responded
    if (upload && (upload->invalid || upload->received != upload->total)) {
        shutter_tests_upload_reset(request);
        request->send(400, "application/json",
                      "{\"success\":false,\"message\":\"Invalid upload\"}");
        return;
    }
    const bool saved = shutter_test_scripts_write(upload ? upload->data : nullptr,
                                                   upload ? upload->total : 0);
    shutter_tests_upload_reset(request);
    if (!saved) {
        request->send(500, "application/json",
                      "{\"success\":false,\"message\":\"Failed to save file\"}");
        return;
    }
    request->send(200, "application/json", "{\"success\":true}");
    LOGI(TAG, "Guided test definitions saved");
}

// ============================================================================
// GET /api/shutter/tests/list — parsed test list as JSON
// ============================================================================

static void handleGetTestList(AsyncWebServerRequest* request) {
    if (!portal_auth_gate(request)) return;

    ShutterTestParseResult* result = (ShutterTestParseResult*)heap_caps_malloc(
        sizeof(ShutterTestParseResult), MALLOC_CAP_SPIRAM);
    if (!result) {
        request->send(500, "application/json", "{\"error\":\"alloc failed\"}");
        return;
    }
    int count = shutter_test_scripts_parse(result);

    BasicJsonDocument<PsramJsonAllocator> doc(2048);
    JsonArray arr = doc.to<JsonArray>();

    for (int i = 0; i < count; i++) {
        JsonObject obj = arr.createNestedObject();
        obj["id"].set(String(result->scripts[i].id));
        obj["name"].set(String(result->scripts[i].name));
        obj["speed_count"]     = result->scripts[i].speed_count;
        obj["shots_per_speed"] = result->scripts[i].shots_per_speed;
    }

    heap_caps_free(result);

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    serializeJson(doc, *response);
    request->send(response);
}

// ============================================================================
// Route registration
// ============================================================================

static void shutter_tests_register(AsyncWebServer* server) {
    server->on("/api/shutter/tests", HTTP_GET, handleGetTests);
    server->on("/api/shutter/tests", HTTP_PUT, handlePutTests,
               nullptr, handlePutTestsBody);
    server->on("/api/shutter/tests/list", HTTP_GET, handleGetTestList);
    LOGI(TAG, "Registered test script routes");
}

REGISTER_ROUTES(shutter_tests_register)

#endif // IS_SHUTTER_TESTER
