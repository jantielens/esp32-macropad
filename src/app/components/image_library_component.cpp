#include "component_registry.h"

#if HAS_IMAGE_LIBRARY

#include "image_library.h"
#include "image_binding.h"
#include "image_library_config.h"
#include "image_library_runtime.h"
#include "image_decoder.h"
#include "storage.h"
#include "web_portal_auth.h"
#include "web_portal_json.h"
#include "web_portal_routes.h"

#include <esp_heap_caps.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <ctype.h>
#include <string.h>

namespace {
constexpr size_t kImageUploadLimit = 4 * 1024 * 1024;
constexpr size_t kImageListJsonCapacity =
    static_cast<size_t>(IMAGE_LIBRARY_ENTRY_LIMIT) * (IMAGE_LIBRARY_PATH_MAX_LEN + 24) * 2 + 512;

struct ImageUploadState {
    File file;
    char destination[IMAGE_LIBRARY_PATH_MAX_LEN];
    char temporary[IMAGE_LIBRARY_PATH_MAX_LEN + 32];
    size_t total;
    size_t received;
    uint8_t header[24];
    uint8_t trailer[4];
    size_t header_length;
    size_t trailer_length;
};

portMUX_TYPE s_image_mutation_lock = portMUX_INITIALIZER_UNLOCKED;
bool s_image_mutation_active = false;

bool image_mutation_begin() {
    portENTER_CRITICAL(&s_image_mutation_lock);
    const bool available = !s_image_mutation_active;
    if (available) s_image_mutation_active = true;
    portEXIT_CRITICAL(&s_image_mutation_lock);
    return available;
}

void image_mutation_end() {
    portENTER_CRITICAL(&s_image_mutation_lock);
    s_image_mutation_active = false;
    portEXIT_CRITICAL(&s_image_mutation_lock);
}

void image_upload_cleanup(AsyncWebServerRequest* request, bool remove_temporary = true, bool release_mutation = true) {
    auto* state = request ? static_cast<ImageUploadState*>(request->_tempObject) : nullptr;
    if (!state) return;
    request->_tempObject = nullptr;
    if (state->file) state->file.close();
    if (remove_temporary && state->temporary[0]) Storage.remove(state->temporary);
    delete state;
    if (release_mutation) image_mutation_end();
}

bool image_library_directory_from_request(AsyncWebServerRequest* request, char* directory, size_t directory_len) {
    const char* requested = IMAGE_LIBRARY_ROOT;
    if (request->hasParam("path")) requested = request->getParam("path")->value().c_str();
    else if (request->hasParam("directory")) requested = request->getParam("directory")->value().c_str();
    if (!ImageLibraryCatalog::is_directory_path(requested)) return false;
    strlcpy(directory, requested, directory_len);
    return true;
}

bool image_upload_filename_is_safe(const char* path) {
    const char* filename = strrchr(path, '/');
    if (!filename || !*(++filename) || *filename == '.') return false;
    const char* extension = strrchr(filename, '.');
    if (!extension || extension == filename) return false;
    for (const char* character = filename; *character; ++character) {
        if (!(isalnum(static_cast<unsigned char>(*character)) || *character == '.' || *character == '-' ||
              *character == '_' || *character == ' ')) return false;
    }
    return true;
}

bool image_upload_contents_are_complete(const ImageUploadState& state) {
    static constexpr uint8_t kJpegHeader[] = {0xFF, 0xD8, 0xFF};
    static constexpr uint8_t kJpegTrailer[] = {0xFF, 0xD9};
    static constexpr uint8_t kPngHeader[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    static constexpr uint8_t kPngTrailer[] = {0xAE, 0x42, 0x60, 0x82};
    if (state.header_length >= sizeof(kJpegHeader) && state.trailer_length >= sizeof(kJpegTrailer) &&
        memcmp(state.header, kJpegHeader, sizeof(kJpegHeader)) == 0) {
        return memcmp(state.trailer + state.trailer_length - sizeof(kJpegTrailer),
                      kJpegTrailer, sizeof(kJpegTrailer)) == 0;
    }
    if (state.header_length < sizeof(state.header) || state.trailer_length != sizeof(kPngTrailer) ||
        memcmp(state.header, kPngHeader, sizeof(kPngHeader)) != 0 ||
        memcmp(state.trailer, kPngTrailer, sizeof(kPngTrailer)) != 0 ||
        state.header[8] != 0 || state.header[9] != 0 || state.header[10] != 0 || state.header[11] != 13 ||
        memcmp(state.header + 12, "IHDR", 4) != 0) {
        return false;
    }
    const uint32_t width = (uint32_t)state.header[16] << 24 | (uint32_t)state.header[17] << 16 |
                           (uint32_t)state.header[18] << 8 | state.header[19];
    const uint32_t height = (uint32_t)state.header[20] << 24 | (uint32_t)state.header[21] << 16 |
                            (uint32_t)state.header[22] << 8 | state.header[23];
    return width > 0 && height > 0 && (uint64_t)width * height <= IMAGE_LIBRARY_MAX_PNG_PIXELS;
}

void image_upload_track_contents(ImageUploadState& state, const uint8_t* data, size_t length) {
    for (size_t index = 0; index < length; ++index) {
        if (state.header_length < sizeof(state.header)) state.header[state.header_length++] = data[index];
        if (state.trailer_length < sizeof(state.trailer)) {
            state.trailer[state.trailer_length++] = data[index];
        } else {
            state.trailer[0] = state.trailer[1];
            state.trailer[1] = state.trailer[2];
            state.trailer[2] = state.trailer[3];
            state.trailer[3] = data[index];
        }
    }
}

void image_library_get_images(AsyncWebServerRequest* request) {
    if (!portal_auth_gate(request)) return;
    char directory[IMAGE_LIBRARY_PATH_MAX_LEN] = {};
    if (!image_library_directory_from_request(request, directory, sizeof(directory))) {
        web_portal_send_json_error(request, 400, "Invalid image directory");
        return;
    }
    portENTER_CRITICAL(&s_image_mutation_lock);
    const bool mutating = s_image_mutation_active;
    portEXIT_CRITICAL(&s_image_mutation_lock);
    if (mutating) {
        web_portal_send_json_error(request, 409, "Image library is busy");
        return;
    }
    auto* snapshot = static_cast<ImageLibrarySnapshot*>(heap_caps_malloc(
        sizeof(ImageLibrarySnapshot), psramFound() ? MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
                                                   : MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    auto doc = make_psram_json_doc(kImageListJsonCapacity);
    if (!snapshot || !doc || doc->capacity() == 0) {
        if (snapshot) heap_caps_free(snapshot);
        web_portal_send_json_error(request, 503, "Image list unavailable");
        return;
    }
    ImageLibraryCatalog catalog;
    const bool available = image_library_discover(directory, &catalog, snapshot);
    JsonObject response = doc->to<JsonObject>();
    response["directory"] = directory;
    response["available"] = available;
    response["overflow"] = snapshot->overflow;
    response["total_found"] = snapshot->total_found;
    JsonArray files = response["files"].to<JsonArray>();
    for (uint8_t index = 0; index < snapshot->count; ++index) files.add(snapshot->paths[index]);
    JsonArray directories = response["directories"].to<JsonArray>();
    if (strcmp(directory, IMAGE_LIBRARY_ROOT) == 0) {
        File root = Storage.open(IMAGE_LIBRARY_ROOT, "r");
        if (root && root.isDirectory()) {
            for (File entry = root.openNextFile(); entry && directories.size() < IMAGE_LIBRARY_ENTRY_LIMIT;
                 entry = root.openNextFile()) {
                char path[IMAGE_LIBRARY_PATH_MAX_LEN] = {};
                if (entry.isDirectory() && image_library_child_path(IMAGE_LIBRARY_ROOT, entry.name(), path, sizeof(path)) &&
                    ImageLibraryCatalog::is_directory_path(path)) directories.add(path);
                entry.close();
            }
            root.close();
        } else if (root) {
            root.close();
        }
    }
    heap_caps_free(snapshot);
    web_portal_send_json_chunked(request, doc);
}

bool image_upload_start(AsyncWebServerRequest* request, size_t total) {
    if (!request->hasParam("path")) {
        web_portal_send_json_error(request, 400, "Missing image path");
        return false;
    }
    const String destination = request->getParam("path")->value();
    if (!ImageLibraryCatalog::is_image_path(destination.c_str()) || !image_upload_filename_is_safe(destination.c_str())) {
        web_portal_send_json_error(request, 400, "Invalid image path");
        return false;
    }
    if (total == 0) {
        web_portal_send_json_error(request, 400, "Empty upload");
        return false;
    }
    if (total > kImageUploadLimit) {
        web_portal_send_json_error(request, 413, "Image upload exceeds 4 MiB");
        return false;
    }
    if (!image_mutation_begin()) {
        web_portal_send_json_error(request, 409, "Image library is busy");
        return false;
    }
    if (Storage.exists(destination.c_str())) {
        image_mutation_end();
        web_portal_send_json_error(request, 409, "Image file already exists");
        return false;
    }
    char parent[IMAGE_LIBRARY_PATH_MAX_LEN] = {};
    strlcpy(parent, destination.c_str(), sizeof(parent));
    *strrchr(parent, '/') = '\0';
    if (!Storage.exists(parent) && !Storage.mkdir(parent)) {
        image_mutation_end();
        web_portal_send_json_error(request, 500, "Unable to create image directory");
        return false;
    }
    auto* state = new ImageUploadState{};
    if (!state) {
        image_mutation_end();
        web_portal_send_json_error(request, 503, "Unable to start image upload");
        return false;
    }
    state->total = total;
    strlcpy(state->destination, destination.c_str(), sizeof(state->destination));
    snprintf(state->temporary, sizeof(state->temporary), "%s.upload-%08lx.tmp", state->destination,
             static_cast<unsigned long>(esp_random()));
    state->file = Storage.open(state->temporary, "w");
    if (!state->file) {
        delete state;
        image_mutation_end();
        web_portal_send_json_error(request, 500, "Unable to create image upload");
        return false;
    }
    request->_tempObject = state;
    request->onDisconnect([request]() { image_upload_cleanup(request); });
    return true;
}

void image_upload_fail(AsyncWebServerRequest* request, int status, const char* message) {
    image_binding_set_error("upload", message);
    image_upload_cleanup(request);
    web_portal_send_json_error(request, status, message);
}

void image_library_post_image(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    if (!portal_auth_gate(request)) {
        image_upload_cleanup(request);
        return;
    }
    if (index == 0 && !image_upload_start(request, total)) return;
    auto* state = static_cast<ImageUploadState*>(request->_tempObject);
    if (!state || state->total != total || index != state->received || len > state->total - state->received ||
        (len > 0 && !data) || state->file.write(data, len) != len) {
        image_upload_fail(request, 400, "Image upload failed");
        return;
    }
    image_upload_track_contents(*state, data, len);
    state->received += len;
    if (state->received != state->total) return;
    state->file.close();
    if (!image_upload_contents_are_complete(*state)) {
        image_upload_fail(request, 400, "Image file is invalid, incomplete, or exceeds the PNG pixel limit");
        return;
    }
    const bool exists = Storage.exists(state->destination);
    if (exists || !Storage.rename(state->temporary, state->destination)) {
        image_upload_fail(request, exists ? 409 : 500, exists ? "Image file already exists" : "Unable to publish image file");
        return;
    }
    state->temporary[0] = '\0';
    image_upload_cleanup(request, false, false);
    const bool reloaded = image_library_runtime_reload();
    image_mutation_end();
    if (!reloaded) {
        web_portal_send_json_error(request, 503, "Image uploaded but library reload failed");
        return;
    }
    request->send(201, "application/json", "{\"success\":true}");
}

void image_library_delete_image(AsyncWebServerRequest* request) {
    if (!portal_auth_gate(request)) return;
    if (!request->hasParam("path")) {
        web_portal_send_json_error(request, 400, "Missing image path");
        return;
    }
    const String path = request->getParam("path")->value();
    if (!ImageLibraryCatalog::is_image_path(path.c_str())) {
        web_portal_send_json_error(request, 400, "Invalid image path");
        return;
    }
    if (!image_mutation_begin()) {
        web_portal_send_json_error(request, 409, "Image library is busy");
        return;
    }
    File file = Storage.open(path.c_str(), "r");
    if (!file || file.isDirectory()) {
        if (file) file.close();
        image_mutation_end();
        web_portal_send_json_error(request, 404, "Image file not found");
        return;
    }
    file.close();
    const bool removed = Storage.remove(path.c_str());
    if (!removed) {
        image_mutation_end();
        web_portal_send_json_error(request, 500, "Unable to delete image file");
        return;
    }
    const bool reloaded = image_library_runtime_reload();
    image_mutation_end();
    if (!reloaded) {
        web_portal_send_json_error(request, 503, "Image deleted but library reload failed");
        return;
    }
    request->send(200, "application/json", "{\"success\":true}");
}

void image_library_routes_register(AsyncWebServer* server) {
    server->on("/api/images", HTTP_GET, image_library_get_images);
    server->on("/api/images", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!portal_auth_gate(request)) return;
    }, nullptr, image_library_post_image);
    server->on("/api/images", HTTP_DELETE, image_library_delete_image);
}

void image_library_get_config(AsyncWebServerRequest* request) {
    ImageLibraryConfig config;
    if (!image_library_config_get(&config)) {
        web_portal_send_json_error(request, 503, "Image library configuration unavailable");
        return;
    }
    auto doc = make_psram_json_doc(256);
    (*doc)["directory"] = config.directory;
    (*doc)["interval_seconds"] = config.interval_seconds;
    web_portal_send_json_sized(request, doc);
}

void image_library_save_config(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    component_handle_save_body(request, data, len, index, total, image_library_config_save_raw, 512);
}
}

static ComponentDef image_library_component = {
    .id = "image-library",
    .category = "display",
    .display_name = "Image Library",
    .nav_order = 40,
    .get_config = image_library_get_config,
    .save_config = nullptr,
    .save_config_body = image_library_save_config,
    .delete_config = nullptr,
    .custom_actions = nullptr,
    .num_custom_actions = 0,
    .fragment_id = "image-library",
};
REGISTER_COMPONENT(image_library);
REGISTER_ROUTES(image_library_routes_register)

#endif // HAS_IMAGE_LIBRARY