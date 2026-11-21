#pragma once

#include <ArduinoJson.h>

inline void pad_redact_image_passwords(JsonObject pad) {
    for (const char* layer : {"buttons", "overlay_buttons"}) {
        for (JsonObject button : pad[layer].as<JsonArray>()) {
            const char* password = button["bg_image_password"] | "";
            const bool password_set = password[0] != '\0';
            button.remove("bg_image_password");
            button["bg_image_password_set"] = password_set;
        }
    }
}
