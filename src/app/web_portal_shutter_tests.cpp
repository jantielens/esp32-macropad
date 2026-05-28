#include "web_portal_shutter_tests.h"

#if IS_SHUTTER_TESTER

#include "shutter_test_scripts.h"
#include "web_portal_auth.h"
#include "component_registry.h"
#include "log_manager.h"

#include "storage.h"
#include "psram_json_allocator.h"
#include <ArduinoJson.h>

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

static void handlePutTestsBody(AsyncWebServerRequest* request,
                                uint8_t* data, size_t len,
                                size_t index, size_t total) {
    // First chunk: open file
    if (index == 0) {
        if (total > 8192) {
            // Safety limit — test files should be small
            request->send(413, "application/json",
                          "{\"success\":false,\"message\":\"File too large\"}");
            return;
        }
        File* f = new File(Storage.open(SHUTTER_TEST_FILE_PATH, "w"));
        if (!*f) {
            delete f;
            request->send(500, "application/json",
                          "{\"success\":false,\"message\":\"Failed to open file\"}");
            return;
        }
        request->_tempObject = f;
    }

    File* f = static_cast<File*>(request->_tempObject);
    if (!f) return;

    f->write(data, len);
}

static void handlePutTests(AsyncWebServerRequest* request) {
    if (!portal_auth_gate(request)) return;

    File* f = static_cast<File*>(request->_tempObject);
    if (f) {
        f->close();
        delete f;
        request->_tempObject = nullptr;
    }

    request->send(200, "application/json", "{\"success\":true}");
    LOGI(TAG, "Guided test definitions saved");

    // Refresh the list provider cache so list widgets pick up new tests
    void list_provider_shutter_tests_refresh();
    list_provider_shutter_tests_refresh();
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

void web_portal_shutter_tests_register_routes(AsyncWebServer* server) {
    server->on("/api/shutter/tests", HTTP_GET, handleGetTests);
    server->on("/api/shutter/tests", HTTP_PUT, handlePutTests,
               nullptr, handlePutTestsBody);
    server->on("/api/shutter/tests/list", HTTP_GET, handleGetTestList);
    LOGI(TAG, "Registered test script routes");
}

#endif // IS_SHUTTER_TESTER
