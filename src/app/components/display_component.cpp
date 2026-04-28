// Display component — migrated from web_portal_display.cpp
// All handlers become custom actions dispatched by (name, method) pair.

#include "component_registry.h"
#include "web_portal_state.h"

#include "log_manager.h"
#include "board_config.h"

#include "display_manager.h"
#include "screen_saver_manager.h"

#include <ArduinoJson.h>

// ---- brightness (PUT, body) ----

static void display_brightness_body(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (index != 0 || index + len != total) return;

    StaticJsonDocument<128> doc;
    DeserializationError error = deserializeJson(doc, data, len);

    if (error) {
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
        return;
    }

    if (!doc.containsKey("brightness")) {
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Missing brightness\"}");
        return;
    }

    int brightness = doc["brightness"];
    if (brightness < 0) brightness = 0;
    if (brightness > 100) brightness = 100;

    LOGI("API", "PUT /api/component/display/brightness: %d%%", brightness);

    screen_saver_manager_set_brightness(brightness);

    char response[64];
    snprintf(response, sizeof(response), "{\"success\":true,\"brightness\":%d}", brightness);
    request->send(200, "application/json", response);
}

// ---- sleep (GET) ----

static void display_sleep_get(AsyncWebServerRequest *request) {
    ScreenSaverStatus status = screen_saver_manager_get_status();

    StaticJsonDocument<256> doc;
    doc["enabled"] = status.enabled;
    doc["state"] = (uint8_t)status.state;
    doc["current_brightness"] = status.current_brightness;
    doc["target_brightness"] = status.target_brightness;
    doc["seconds_until_sleep"] = status.seconds_until_sleep;

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    serializeJson(doc, *response);
    request->send(response);
}

// ---- sleep (POST) ----

static void display_sleep_post(AsyncWebServerRequest *request) {
    LOGI("API", "POST /api/component/display/sleep");
    screen_saver_manager_sleep_now();
    request->send(200, "application/json", "{\"success\":true}");
}

// ---- wake (POST) ----

static void display_wake_post(AsyncWebServerRequest *request) {
    LOGI("API", "POST /api/component/display/wake");
    screen_saver_manager_wake();
    request->send(200, "application/json", "{\"success\":true}");
}

// ---- activity (POST) ----

static void display_activity_post(AsyncWebServerRequest *request) {
    bool wake = false;
    if (request->hasParam("wake")) {
        wake = (request->getParam("wake")->value() == "1");
    }

    LOGI("API", "POST /api/component/display/activity (wake=%d)", (int)wake);
    screen_saver_manager_notify_activity(wake);
    request->send(200, "application/json", "{\"success\":true}");
}

// ---- screen (PUT, body) ----

static void display_screen_body(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (index != 0 || index + len != total) return;

    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, data, len);

    if (error) {
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
        return;
    }

    if (!doc.containsKey("screen")) {
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Missing screen ID\"}");
        return;
    }

    const char *screen_id = doc["screen"];
    if (!screen_id || strlen(screen_id) == 0) {
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid screen ID\"}");
        return;
    }

    LOGI("API", "PUT /api/component/display/screen: %s", screen_id);

    bool success = false;
    display_manager_show_screen(screen_id, &success);

    if (success) {
        screen_saver_manager_notify_activity(true);
    }

    if (success) {
        char response[96];
        snprintf(response, sizeof(response), "{\"success\":true,\"screen\":\"%s\"}", screen_id);
        request->send(200, "application/json", response);
    } else {
        request->send(404, "application/json", "{\"success\":false,\"message\":\"Screen not found\"}");
    }
}

// ---- Custom actions table ----

static const ComponentAction display_actions[] = {
    {"brightness", HTTP_PUT,  nullptr,              display_brightness_body},
    {"sleep",      HTTP_GET,  display_sleep_get,    nullptr},
    {"sleep",      HTTP_POST, display_sleep_post,   nullptr},
    {"wake",       HTTP_POST, display_wake_post,    nullptr},
    {"activity",   HTTP_POST, display_activity_post, nullptr},
    {"screen",     HTTP_PUT,  nullptr,              display_screen_body},
};

static ComponentDef display_component = {
    .id = "display",
    .category = "display",
    .display_name = "Display",
    .nav_order = 10,
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = display_actions,
    .num_custom_actions = sizeof(display_actions) / sizeof(display_actions[0]),
    .fragment_id = "brightness"
};

REGISTER_COMPONENT(display);
