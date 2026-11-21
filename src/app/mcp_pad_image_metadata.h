#ifndef MCP_PAD_IMAGE_METADATA_H
#define MCP_PAD_IMAGE_METADATA_H

#include "pad_config.h"
#include <ArduinoJson.h>
#include <stdint.h>

inline void mcp_pad_image_metadata_emit(JsonObject button, bool remote_images,
                                        bool local_images, bool native_extensions) {
    JsonObject images = button.createNestedObject("background_images");
    images["has_image_fetch"] = remote_images;
    images["has_image_library"] = local_images;
    images["availability_note"] = "These flags describe firmware support, matching /api/info. A remote URL must be reachable by the device; a local path must identify an existing image library file. They do not guarantee a successful fetch or decode.";
    JsonArray incompatible = images.createNestedArray("incompatible_widgets");
    if (native_extensions) incompatible.add("external");
    images["compatibility_note"] = "The external (Extension) widget replaces button content; the portal hides background-image controls for it, independently of image capability support. Choose a different widget or no widget to restore those controls; hiding them does not clear configured image values.";
    images["authentication_note"] = "Binding tokens in bg_image_url cannot be combined with bg_image_user, bg_image_password, or bg_image_password_set=true (a retained password). Authentication fields are static, not bindings. GET /api/pad redacts the password and exposes bg_image_password_set instead.";

    JsonArray fields = button["fields"].as<JsonArray>();
    JsonObject metadata = button.createNestedObject("image_fields");
    auto text_field = [&](const char* name, uint16_t capacity, bool bindable, bool available) {
        fields.add(name);
        JsonObject field = metadata.createNestedObject(name);
        field["type"] = "string";
        field["default"] = "";
        field["max_length"] = capacity - 1;
        field["length_unit"] = "UTF-8 bytes, excluding NUL";
        field["bindable"] = bindable;
        field["available"] = available;
        return field;
    };
    JsonObject path = text_field("bg_image_path", CONFIG_BG_IMAGE_PATH_MAX_LEN, true, local_images);
    path["description"] = "Canonical /images/name.png or /images/album/name.jpg path, or a binding resolving to one. Empty disables the local background.";
    JsonObject url = text_field("bg_image_url", CONFIG_BG_IMAGE_URL_MAX_LEN, true, remote_images);
    url["description"] = "Remote image or camera-feed URL; may contain binding tokens. Empty disables the remote background. A changed resolved URL replaces the current image independently of the refresh interval; empty or unresolved results hide it.";
    text_field("bg_image_user", CONFIG_BG_IMAGE_USER_MAX_LEN, false, remote_images)
        ["description"] = "HTTP Basic Auth username for a static remote URL.";
    text_field("bg_image_password", CONFIG_BG_IMAGE_PASS_MAX_LEN, false, remote_images)
        ["description"] = "HTTP Basic Auth password for a static remote URL; redacted by GET /api/pad.";
    fields.add("bg_image_password_set");
    JsonObject retained = metadata.createNestedObject("bg_image_password_set");
    retained["type"] = "boolean";
    retained["default"] = false;
    retained["bindable"] = false;
    retained["available"] = remote_images;
    retained["description"] = "GET /api/pad indicator for a stored password; true on a subsequent portal save retains it. This is not an MCP password-retention mechanism. Not valid with a binding-based remote URL.";
    fields.add("bg_image_interval_ms");
    JsonObject interval = metadata.createNestedObject("bg_image_interval_ms");
    interval["type"] = "integer";
    interval["default"] = 0;
    interval["min"] = 0;
    interval["max"] = UINT32_MAX;
    interval["bindable"] = false;
    interval["available"] = remote_images;
    interval["description"] = "Remote refresh interval in milliseconds. 0 fetches a snapshot once per URL; for MJPEG, 0 streams at the server's frame rate. Positive values request periodic snapshots or throttle stream frames.";
    fields.add("bg_image_fit");
    JsonObject fit = metadata.createNestedObject("bg_image_fit");
    fit["type"] = "string";
    fit["default"] = "fill-center";
    fit["bindable"] = false;
    fit["available"] = remote_images || local_images;
    JsonArray choices = fit.createNestedArray("choices");
    const char* modes[] = {"fill-center", "fill-top", "fill-bottom",
                            "fill-left", "fill-right", "letterbox"};
    for (const char* choice : modes) choices.add(choice);
    fit["description"] = "Shared scale mode for local and remote backgrounds. Fill modes crop at the selected alignment; letterbox preserves the whole image with button-color bars.";
}

#endif
