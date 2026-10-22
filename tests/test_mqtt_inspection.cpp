#include "mqtt_sub_store_test_support.h"
#include "Arduino.h"
#include "ESPAsyncWebServer.h"
#include "esp_heap_caps.h"
#include "psram_json_allocator.h"

#include <ArduinoJson.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

class String : public std::string {
public:
    String(const std::string& value) : std::string(value) {}
};

class AsyncWebServer {
public:
    void on(const char*, WebRequestMethodComposite,
            ArRequestHandlerFunction, std::nullptr_t = nullptr,
            ArBodyHandlerFunction = {}) {}
};

#define WEB_PORTAL_JSON_H

static std::shared_ptr<BasicJsonDocument<PsramJsonAllocator>> make_psram_json_doc(size_t capacity) {
    return std::make_shared<BasicJsonDocument<PsramJsonAllocator>>(capacity);
}

template <typename TDoc>
static void web_portal_send_json_sized(AsyncWebServerRequest* request,
                                      const std::shared_ptr<TDoc>& doc, int status) {
    std::string body;
    serializeJson(*doc, body);
    request->send(status, "application/json", body.c_str());
}

#include "../src/app/mqtt_inspection.cpp"

MqttManager mqtt_manager;

namespace {
bool allocation_fails = false;
bool mutex_fails = false;
std::vector<uint32_t> allocation_caps;
uint32_t next_id = 1;
uint32_t clock_ms = 0;
bool ota_active = false;
size_t interests = 0;
}

bool psramFound() { return HAS_PSRAM; }
void* heap_caps_malloc(size_t size, uint32_t caps) {
    allocation_caps.push_back(caps);
    return allocation_fails ? nullptr : malloc(size);
}
void heap_caps_free(void* memory) { free(memory); }
extern "C" unsigned long millis() { return clock_ms; }
uint32_t esp_random() { return next_id++; }
bool ota_activity_is_active() { return ota_active; }
SemaphoreHandle_t test_freertos_create_mutex() {
    if (mutex_fails) return nullptr;
    auto* semaphore = new StaticSemaphore_t{};
    semaphore->signaled = true;
    return semaphore;
}
void test_freertos_delete_mutex(SemaphoreHandle_t semaphore) { delete semaphore; }
bool portal_auth_gate(AsyncWebServerRequest* request) {
    if (!request->authorized) request->send(401);
    return request->authorized;
}
bool mqtt_sub_store_active() { return true; }
bool mqtt_sub_store_inspection_acquire(const char*, bool* cached) {
    if (cached) *cached = false;
    ++interests;
    return true;
}
bool mqtt_sub_store_inspection_release(const char*) {
    --interests;
    return true;
}
bool mqtt_sub_store_inspection_snapshot(const char*, uint8_t* payload,
                                        size_t, size_t* length,
                                        bool* received, bool* truncated) {
    payload[0] = '4';
    payload[1] = '2';
    *length = 2;
    *received = true;
    *truncated = false;
    return true;
}

class MqttInspection : public testing::Test {
protected:
    void SetUp() override {
        allocation_fails = false;
        mutex_fails = false;
        allocation_caps.clear();
        clock_ms = 0;
        ota_active = false;
        interests = 0;
    }

    void TearDown() override {
        if (g_runtime) {
            g_runtime->~InspectionRuntime();
            heap_caps_free(g_runtime);
            g_runtime = nullptr;
        }
        delete g_inspection_mutex;
        delete g_body_mutex;
        g_inspection_mutex = nullptr;
        g_body_mutex = nullptr;
    }

    static void post(AsyncWebServerRequest& request) {
        std::string body = "{\"topic\":\"sensors/test\"}";
        handle_post_body(&request, reinterpret_cast<uint8_t*>(&body[0]),
                         body.size(), 0, body.size());
    }
};

TEST_F(MqttInspection, AllocatesOnceInBoardMemoryRegion) {
    mqtt_inspection_init();
    ASSERT_TRUE(inspection_ready());
    ASSERT_EQ(allocation_caps.size(), 1U);
    EXPECT_EQ(allocation_caps[0],
              static_cast<uint32_t>(MALLOC_CAP_8BIT |
                  (HAS_PSRAM ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL)));
    mqtt_inspection_init();
    EXPECT_EQ(allocation_caps.size(), 1U);
}

TEST_F(MqttInspection, AllocationFailureDoesNotFallbackAndEndpointsFailExplicitly) {
    allocation_fails = true;
    mqtt_inspection_init();
    ASSERT_FALSE(inspection_ready());
    ASSERT_EQ(allocation_caps.size(), 1U);
    EXPECT_EQ(allocation_caps[0],
              static_cast<uint32_t>(MALLOC_CAP_8BIT |
                  (HAS_PSRAM ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL)));
    mqtt_inspection_tick();

    AsyncWebServerRequest create;
    post(create);
    EXPECT_EQ(create.response_code, 503);
    EXPECT_NE(create.response_body.find("inspection_unavailable"), std::string::npos);
    AsyncWebServerRequest poll;
    poll.parameters["handle"].text = "1";
    handle_get(&poll);
    EXPECT_EQ(poll.response_code, 503);
    AsyncWebServerRequest release;
    release.parameters["handle"].text = "1";
    handle_delete(&release);
    EXPECT_EQ(release.response_code, 503);
    EXPECT_EQ(interests, 0U);
    EXPECT_EQ(allocation_caps.size(), 1U);

    allocation_fails = false;
    mqtt_inspection_init();
    EXPECT_TRUE(inspection_ready());
    EXPECT_EQ(allocation_caps.size(), 2U);
}

TEST_F(MqttInspection, MutexFailureLeavesRuntimeUnavailableUntilRetry) {
    mutex_fails = true;
    mqtt_inspection_init();
    ASSERT_NE(g_runtime, nullptr);
    ASSERT_FALSE(inspection_ready());
    mqtt_inspection_tick();
    AsyncWebServerRequest create;
    post(create);
    EXPECT_EQ(create.response_code, 503);
    EXPECT_EQ(interests, 0U);

    mutex_fails = false;
    mqtt_inspection_init();
    EXPECT_TRUE(inspection_ready());
    EXPECT_EQ(allocation_caps.size(), 1U);
}

TEST_F(MqttInspection, PreservesCapacitySamplesReleaseAndExpiry) {
    mqtt_inspection_init();
    uint32_t first_id = 0;
    uint32_t expiring_id = 0;
    for (int i = 0; i < 4; ++i) {
        AsyncWebServerRequest create;
        post(create);
        ASSERT_EQ(create.response_code, 202);
        JsonDocument response;
        ASSERT_FALSE(deserializeJson(response, create.response_body));
        if (i == 0) first_id = response["handle"].as<uint32_t>();
        if (i == 1) expiring_id = response["handle"].as<uint32_t>();
    }
    AsyncWebServerRequest excess;
    post(excess);
    EXPECT_EQ(excess.response_code, 429);
    EXPECT_EQ(interests, 4U);

    mqtt_inspection_tick();
    AsyncWebServerRequest poll;
    poll.parameters["handle"].text = std::to_string(first_id);
    handle_get(&poll);
    ASSERT_EQ(poll.response_code, 200);
    JsonDocument sample;
    ASSERT_FALSE(deserializeJson(sample, poll.response_body));
    EXPECT_STREQ(sample["status"], "received");
    EXPECT_STREQ(sample["sample"], "42");

    AsyncWebServerRequest release;
    release.parameters["handle"].text = std::to_string(first_id);
    handle_delete(&release);
    EXPECT_EQ(release.response_code, 202);
    mqtt_inspection_tick();
    EXPECT_EQ(interests, 3U);

    clock_ms = 30001;
    mqtt_inspection_tick();
    EXPECT_EQ(interests, 0U);
    AsyncWebServerRequest expired;
    expired.parameters["handle"].text = std::to_string(expiring_id);
    handle_get(&expired);
    EXPECT_EQ(expired.response_code, 410);
}

TEST_F(MqttInspection, RejectsNewAcquisitionsDuringOta) {
    mqtt_inspection_init();
    ota_active = true;
    AsyncWebServerRequest create;
    post(create);
    EXPECT_EQ(create.response_code, 503);
    EXPECT_NE(create.response_body.find("ota_active"), std::string::npos);
    EXPECT_EQ(interests, 0U);
}
