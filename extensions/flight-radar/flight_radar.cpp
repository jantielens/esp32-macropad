#include "native_extension_api.h"

extern "C" const NativeExtensionDescriptor native_extension_descriptor = {
    NATIVE_EXTENSION_DESCRIPTOR_MAGIC, NATIVE_EXTENSION_ABI_VERSION,
    NATIVE_EXTENSION_TARGET_ABI, "flight-radar", "1.1.0", "Flight Radar",
};

namespace {

constexpr uint16_t MAX_PLANES = 100;
constexpr uint8_t MAX_VIEWS = 4;
constexpr uint8_t MAX_LABELS = 8;
constexpr size_t RESPONSE_CAPACITY = 64 * 1024;
constexpr uint32_t REFRESH_INTERVAL_MS = 10000;
constexpr uint32_t HTTP_TIMEOUT_MS = 15000;
constexpr float EARTH_RADIUS_KM = 6371.0f;
constexpr float PI = 3.14159265358979323846f;
constexpr int32_t RADAR_INSET_PX = 12;
constexpr int32_t FOOTER_HEIGHT_PX = 28;
constexpr int32_t NEAR_HOME_MARKER_RADIUS_PX = 14;
constexpr int32_t MARKER_TIP_PX = 11;
constexpr int32_t MARKER_TAIL_PX = 7;
constexpr int32_t MARKER_HALF_WIDTH_PX = 7;
constexpr uint8_t MARKER_LINE_WIDTH_PX = 1;
constexpr int32_t MARKER_LABEL_GAP_PX = 8;

struct RadarConfig {
    float latitude;
    float longitude;
    float range_km;
    uint16_t max_planes;
    uint16_t refresh_interval_secs;
    char latitude_text[20];
    char longitude_text[20];
};

struct Plane {
    char callsign[16];
    char registration[16];
    char aircraft_type[8];
    float latitude;
    float longitude;
    float distance_km;
    float heading_degrees;
    float altitude_feet;
    float speed_knots;
    uint8_t has_altitude;
    uint8_t has_speed;
};

struct RadarView {
    uint8_t active;
    uint32_t instance_id;
    void* canvas;
    void* status_label;
    void* plane_labels[MAX_LABELS];
    void* canvas_buffer;
    uint16_t width;
    uint16_t height;
    uint32_t rendered_version;
};

struct RadarService {
    const NativeExtensionHostApi* host;
    void* extension_context;
    void* mutex;
    void* worker_task;
    uint8_t worker_started;
    uint8_t active_views;
    RadarConfig config;
    Plane snapshot[MAX_PLANES];
    Plane render[MAX_PLANES];
    Plane parse[MAX_PLANES];
    uint16_t count;
    uint16_t in_range;
    uint32_t version;
    uint32_t refresh_ms;
    uint32_t last_success_ms;
    uint8_t has_success;
    char status[72];
    uint8_t* response;
    RadarView views[MAX_VIEWS];
};

void clear_bytes(void* data, size_t length) {
    uint8_t* bytes = static_cast<uint8_t*>(data);
    for (size_t index = 0; index < length; ++index) bytes[index] = 0;
}

void copy_text(char* out, size_t capacity, const char* input) {
    if (!out || capacity == 0) return;
    size_t index = 0;
    if (input) while (input[index] && index + 1 < capacity) { out[index] = input[index]; ++index; }
    out[index] = '\0';
}

void append_text(char* out, size_t capacity, const char* input) {
    size_t offset = 0;
    while (offset < capacity && out[offset]) ++offset;
    if (offset == capacity) return;
    size_t index = 0;
    while (input && input[index] && offset + 1 < capacity) out[offset++] = input[index++];
    out[offset] = '\0';
}

void append_uint(char* out, size_t capacity, uint32_t value) {
    char reversed[12];
    uint8_t count = 0;
    do { reversed[count++] = static_cast<char>('0' + value % 10); value /= 10; } while (value && count < sizeof(reversed));
    while (count) { char piece[2] = {reversed[--count], '\0'}; append_text(out, capacity, piece); }
}

void append_fixed_1(char* out, size_t capacity, float value) {
    if (value < 0.0f) { append_text(out, capacity, "-"); value = -value; }
    uint32_t whole = static_cast<uint32_t>(value);
    uint32_t tenth = static_cast<uint32_t>((value - whole) * 10.0f + 0.5f);
    if (tenth == 10) { ++whole; tenth = 0; }
    append_uint(out, capacity, whole);
    append_text(out, capacity, ".");
    append_uint(out, capacity, tenth);
}

bool is_space(char value) { return value == ' ' || value == '\n' || value == '\r' || value == '\t'; }

const char* skip_space(const char* value, const char* end) {
    while (value < end && is_space(*value)) ++value;
    return value;
}

bool key_matches(const char* value, const char* key) {
    size_t index = 0;
    while (key[index] && value[index] == key[index]) ++index;
    return key[index] == '\0' && value[index] == '"';
}

const char* find_value(const char* begin, const char* end, const char* key) {
    for (const char* cursor = begin; cursor + 2 < end; ++cursor) {
        if (*cursor != '"' || !key_matches(cursor + 1, key)) continue;
        cursor += 2;
        while (cursor < end && *cursor != ':') ++cursor;
        if (cursor >= end) return nullptr;
        return skip_space(cursor + 1, end);
    }
    return nullptr;
}

const char* find_object_value(const char* begin, const char* end, const char* key) {
    if (!begin || begin >= end || *begin != '{') return nullptr;
    uint16_t depth = 0;
    bool quoted = false;
    for (const char* cursor = begin; cursor + 2 < end; ++cursor) {
        if (*cursor == '"' && (cursor == begin || cursor[-1] != '\\')) {
            if (!quoted && depth == 1 && key_matches(cursor + 1, key)) {
                const char* value = cursor + 2;
                while (value < end && *value != ':') ++value;
                return value < end ? skip_space(value + 1, end) : nullptr;
            }
            quoted = !quoted;
            continue;
        }
        if (quoted) continue;
        if (*cursor == '{' || *cursor == '[') ++depth;
        else if ((*cursor == '}' || *cursor == ']') && depth) --depth;
    }
    return nullptr;
}

bool parse_float(const char* value, const char* end, float* out) {
    if (!value || value >= end || !out) return false;
    bool negative = false;
    if (*value == '-') { negative = true; ++value; }
    bool digits = false;
    float result = 0.0f;
    while (value < end && *value >= '0' && *value <= '9') { result = result * 10.0f + (*value++ - '0'); digits = true; }
    if (value < end && *value == '.') {
        float divisor = 10.0f;
        ++value;
        while (value < end && *value >= '0' && *value <= '9') { result += (*value++ - '0') / divisor; divisor *= 10.0f; digits = true; }
    }
    if (!digits) return false;
    *out = negative ? -result : result;
    return true;
}

bool parse_uint(const char* value, const char* end, uint16_t* out) {
    if (!value || !out) return false;
    uint32_t result = 0;
    bool digits = false;
    while (value < end && *value >= '0' && *value <= '9') { result = result * 10 + (*value++ - '0'); digits = true; }
    if (!digits || result > 65535) return false;
    *out = static_cast<uint16_t>(result);
    return true;
}

void copy_json_string(char* out, size_t capacity, const char* value, const char* end, const char* fallback) {
    if (!value || value >= end || *value != '"') { copy_text(out, capacity, fallback); return; }
    ++value;
    size_t index = 0;
    while (value < end && *value != '"' && index + 1 < capacity) {
        if (*value == '\\' && value + 1 < end) ++value;
        out[index++] = *value++;
    }
    while (index && is_space(out[index - 1])) --index;
    out[index] = '\0';
    if (!out[0]) copy_text(out, capacity, fallback);
}

void copy_config_token(char* out, size_t capacity, const char* value, const char* end) {
    if (!value || capacity == 0) return;
    if (*value == '"') ++value;
    size_t index = 0;
    while (value < end && *value != '"' && *value != ',' && *value != '}' && !is_space(*value) && index + 1 < capacity) out[index++] = *value++;
    out[index] = '\0';
}

bool parse_config(const char* json, RadarConfig* config) {
    if (!config) return false;
    const char* end = json;
    while (end && *end) ++end;
    config->latitude = 51.2189473f;
    config->longitude = 5.4216694f;
    config->range_km = 50.0f;
    config->max_planes = 20;
    config->refresh_interval_secs = REFRESH_INTERVAL_MS / 1000;
    copy_text(config->latitude_text, sizeof(config->latitude_text), "51.2189473");
    copy_text(config->longitude_text, sizeof(config->longitude_text), "5.4216694");
    if (!json || !json[0]) return true;

    const char* value = find_value(json, end, "lat");
    if (value && parse_float(value, end, &config->latitude)) copy_config_token(config->latitude_text, sizeof(config->latitude_text), value, end);
    value = find_value(json, end, "lon");
    if (value && parse_float(value, end, &config->longitude)) copy_config_token(config->longitude_text, sizeof(config->longitude_text), value, end);
    value = find_value(json, end, "range_km");
    if (!value) value = find_value(json, end, "range");
    if (value) parse_float(value, end, &config->range_km);
    value = find_value(json, end, "max_planes");
    if (value) parse_uint(value, end, &config->max_planes);
    value = find_value(json, end, "interval");
    if (value) parse_uint(value, end, &config->refresh_interval_secs);

    if (config->latitude < -90.0f || config->latitude > 90.0f || config->longitude < -180.0f || config->longitude > 180.0f ||
        config->range_km < 1.0f || config->range_km > 463.0f || config->max_planes < 1 || config->max_planes > MAX_PLANES ||
        config->refresh_interval_secs < 1 || config->refresh_interval_secs > 3600) return false;
    return true;
}

float radians(float degrees) { return degrees * PI / 180.0f; }

float haversine_km(const NativeExtensionHostApi* host, float home_lat, float home_lon, float lat, float lon) {
    const float delta_lat = radians(lat - home_lat);
    const float delta_lon = radians(lon - home_lon);
    const float sin_lat = host->math_sin(delta_lat * 0.5f);
    const float sin_lon = host->math_sin(delta_lon * 0.5f);
    const float a = sin_lat * sin_lat + host->math_cos(radians(home_lat)) * host->math_cos(radians(lat)) * sin_lon * sin_lon;
    return EARTH_RADIUS_KM * 2.0f * host->math_atan2(host->math_sqrt(a), host->math_sqrt(1.0f - a));
}

float bearing_radians(const NativeExtensionHostApi* host, const RadarConfig& config, const Plane& plane) {
    const float delta_lon = radians(plane.longitude - config.longitude);
    const float target_lat = radians(plane.latitude);
    const float home_lat = radians(config.latitude);
    const float y = host->math_sin(delta_lon) * host->math_cos(target_lat);
    const float x = host->math_cos(home_lat) * host->math_sin(target_lat) -
                    host->math_sin(home_lat) * host->math_cos(target_lat) * host->math_cos(delta_lon);
    return host->math_atan2(y, x);
}

const char* find_object_end(const char* begin, const char* end) {
    uint16_t depth = 0;
    bool quoted = false;
    for (const char* cursor = begin; cursor < end; ++cursor) {
        if (*cursor == '"' && (cursor == begin || cursor[-1] != '\\')) quoted = !quoted;
        if (quoted) continue;
        if (*cursor == '{') ++depth;
        else if (*cursor == '}' && --depth == 0) return cursor + 1;
    }
    return nullptr;
}

bool parse_plane(const NativeExtensionHostApi* host, const char* begin, const char* end, const RadarConfig& config, Plane* plane) {
    clear_bytes(plane, sizeof(*plane));
    const char* value = find_object_value(begin, end, "lat");
    if (!parse_float(value, end, &plane->latitude)) return false;
    value = find_object_value(begin, end, "lon");
    if (!parse_float(value, end, &plane->longitude)) return false;
    plane->distance_km = haversine_km(host, config.latitude, config.longitude, plane->latitude, plane->longitude);
    if (plane->distance_km > config.range_km) return false;
    copy_json_string(plane->callsign, sizeof(plane->callsign), find_object_value(begin, end, "flight"), end, "Unknown");
    copy_json_string(plane->registration, sizeof(plane->registration), find_object_value(begin, end, "r"), end, "Unknown");
    copy_json_string(plane->aircraft_type, sizeof(plane->aircraft_type), find_object_value(begin, end, "t"), end, "Unknown");
    value = find_object_value(begin, end, "track");
    parse_float(value, end, &plane->heading_degrees);
    value = find_object_value(begin, end, "alt_baro");
    plane->has_altitude = parse_float(value, end, &plane->altitude_feet) ? 1 : 0;
    value = find_object_value(begin, end, "gs");
    plane->has_speed = parse_float(value, end, &plane->speed_knots) ? 1 : 0;
    return true;
}

uint16_t parse_response(const NativeExtensionHostApi* host, const char* response, size_t length,
                        const RadarConfig& config, Plane* planes, uint16_t* in_range) {
    const char* end = response + length;
    const char* array = find_value(response, end, "ac");
    if (!array || *array != '[') return 0;
    uint16_t count = 0;
    *in_range = 0;
    for (const char* cursor = array + 1; cursor < end;) {
        while (cursor < end && *cursor != '{' && *cursor != ']') ++cursor;
        if (cursor >= end || *cursor == ']') break;
        const char* object_end = find_object_end(cursor, end);
        if (!object_end) break;
        Plane plane = {};
        if (parse_plane(host, cursor, object_end, config, &plane)) {
            ++*in_range;
            uint16_t insert = 0;
            if (count < config.max_planes) {
                insert = count++;
            } else {
                insert = config.max_planes - 1;
                if (plane.distance_km >= planes[insert].distance_km) { cursor = object_end; continue; }
            }
            while (insert > 0 && plane.distance_km < planes[insert - 1].distance_km) { if (insert < config.max_planes) planes[insert] = planes[insert - 1]; --insert; }
            planes[insert] = plane;
        }
        cursor = object_end;
    }
    return count;
}

void set_status(RadarService* service, const char* text) { copy_text(service->status, sizeof(service->status), text); }

void log_refresh(RadarService* service, uint32_t refresh_ms) {
    char message[120] = "flight radar: total=";
    append_uint(message, sizeof(message), refresh_ms);
    append_text(message, sizeof(message), "ms in_range=");
    append_uint(message, sizeof(message), service->in_range);
    append_text(message, sizeof(message), " displayed=");
    append_uint(message, sizeof(message), service->count);
    service->host->log(NATIVE_EXTENSION_LOG_INFO, message);
    for (uint16_t index = 0; index < service->count; ++index) {
        const Plane& plane = service->snapshot[index];
        char plane_message[96] = "flight radar: ";
        append_text(plane_message, sizeof(plane_message), plane.callsign);
        append_text(plane_message, sizeof(plane_message), " lat=");
        append_fixed_1(plane_message, sizeof(plane_message), plane.latitude);
        append_text(plane_message, sizeof(plane_message), " lon=");
        append_fixed_1(plane_message, sizeof(plane_message), plane.longitude);
        append_text(plane_message, sizeof(plane_message), " dist=");
        append_fixed_1(plane_message, sizeof(plane_message), plane.distance_km);
        append_text(plane_message, sizeof(plane_message), "km");
        service->host->log(NATIVE_EXTENSION_LOG_INFO, plane_message);
    }
}

void log_config(const NativeExtensionHostApi* host, const RadarConfig& config) {
    char message[112] = "flight radar: config lat=";
    append_text(message, sizeof(message), config.latitude_text);
    append_text(message, sizeof(message), " lon=");
    append_text(message, sizeof(message), config.longitude_text);
    append_text(message, sizeof(message), " range=");
    append_fixed_1(message, sizeof(message), config.range_km);
    append_text(message, sizeof(message), "km max=");
    append_uint(message, sizeof(message), config.max_planes);
    append_text(message, sizeof(message), " interval=");
    append_uint(message, sizeof(message), config.refresh_interval_secs);
    append_text(message, sizeof(message), "s");
    host->log(NATIVE_EXTENSION_LOG_INFO, message);
}

void refresh(RadarService* service) {
    const NativeExtensionHostApi* host = service->host;
    RadarConfig config = {};
    if (!host->mutex_lock(service->mutex, 100)) return;
    config = service->config;
    host->mutex_unlock(service->mutex);

    char url[112] = "https://api.adsb.lol/v2/point/";
    append_text(url, sizeof(url), config.latitude_text);
    append_text(url, sizeof(url), "/");
    append_text(url, sizeof(url), config.longitude_text);
    append_text(url, sizeof(url), "/");
    uint16_t radius_nm = static_cast<uint16_t>(config.range_km / 1.852f);
    if (static_cast<float>(radius_nm) * 1.852f < config.range_km) ++radius_nm;
    if (radius_nm < 1) radius_nm = 1;
    if (radius_nm > 250) radius_nm = 250;
    append_uint(url, sizeof(url), radius_nm);

    const uint32_t started = host->millis();
    NativeExtensionHttpResult result = {};
    if (!host->http_get(url, service->response, RESPONSE_CAPACITY - 1, HTTP_TIMEOUT_MS, &result) || result.status_code != 200 || result.truncated) {
        if (host->mutex_lock(service->mutex, 100)) {
            char error[72] = "ADSB request failed: HTTP ";
            append_uint(error, sizeof(error), result.status_code > 0 ? static_cast<uint32_t>(result.status_code) : 0);
            set_status(service, error);
            ++service->version;
            host->mutex_unlock(service->mutex);
        }
        host->log(NATIVE_EXTENSION_LOG_WARN, "flight radar ADSB request failed");
        host->core->status_set(service->extension_context, NATIVE_EXTENSION_RUNTIME_ERROR,
                       "ADSB request failed");
        return;
    }
    service->response[result.body_length] = '\0';
    clear_bytes(service->parse, sizeof(service->parse));
    uint16_t in_range = 0;
    const uint16_t count = parse_response(host, reinterpret_cast<const char*>(service->response), result.body_length,
                                          config, service->parse, &in_range);
    if (count == 0 && result.body_length > 16) {
        // An empty result is valid; malformed root documents are shown as an error.
        const char* array = find_value(reinterpret_cast<const char*>(service->response), reinterpret_cast<const char*>(service->response) + result.body_length, "ac");
        if (!array) {
            if (host->mutex_lock(service->mutex, 100)) { set_status(service, "ADSB JSON parse failed"); ++service->version; host->mutex_unlock(service->mutex); }
            host->log(NATIVE_EXTENSION_LOG_WARN, "flight radar ADSB JSON parse failed");
            host->core->status_set(service->extension_context, NATIVE_EXTENSION_RUNTIME_ERROR,
                                   "ADSB JSON parse failed");
            return;
        }
    }
    const uint32_t elapsed = host->millis() - started;
    if (host->mutex_lock(service->mutex, 100)) {
        for (uint16_t index = 0; index < count; ++index) service->snapshot[index] = service->parse[index];
        service->count = count;
        service->in_range = in_range;
        service->refresh_ms = elapsed;
        service->last_success_ms = host->millis();
        service->has_success = 1;
        set_status(service, count ? "Live ADS-B" : "No aircraft in range");
        host->core->status_set(service->extension_context, NATIVE_EXTENSION_RUNTIME_RUNNING,
                       count ? "Live ADS-B" : "No aircraft in range");
        ++service->version;
        log_refresh(service, elapsed);
        host->mutex_unlock(service->mutex);
    }
}

void radar_worker(void* context) {
    RadarService* service = static_cast<RadarService*>(context);
    service->host->log(NATIVE_EXTENSION_LOG_INFO, "flight radar: worker started");
    service->host->core->status_set(service->extension_context, NATIVE_EXTENSION_RUNTIME_RUNNING,
                                    "Radar worker started");
    while (true) {
        bool active = false;
        uint32_t interval_ms = 500;
        if (service->host->mutex_lock(service->mutex, 100)) {
            active = service->active_views > 0;
            if (active) interval_ms = static_cast<uint32_t>(service->config.refresh_interval_secs) * 1000;
            service->host->mutex_unlock(service->mutex);
        }
        const uint32_t started = service->host->millis();
        if (active) refresh(service);
        const uint32_t elapsed = service->host->millis() - started;
        service->host->delay_ms(active && elapsed < interval_ms ? interval_ms - elapsed : 500);
    }
}

RadarService* get_service(const NativeExtensionHostApi* host, void* extension_context) {
    RadarService* service = static_cast<RadarService*>(host->context_get_data(extension_context));
    if (service) return service;
    service = static_cast<RadarService*>(host->alloc(sizeof(RadarService)));
    if (!service) return nullptr;
    clear_bytes(service, sizeof(*service));
    service->host = host;
    service->extension_context = extension_context;
    service->mutex = host->mutex_create();
    service->response = static_cast<uint8_t*>(host->alloc(RESPONSE_CAPACITY));
    if (!service->mutex || !service->response) {
        host->log(NATIVE_EXTENSION_LOG_ERROR, "flight radar: service allocation failed");
        if (service->response) host->free(service->response);
        host->free(service);
        return nullptr;
    }
    host->context_set_data(extension_context, service);
    host->log(NATIVE_EXTENSION_LOG_INFO, "flight radar: service ready");
    return service;
}

RadarView* create_view(RadarService* service, uint32_t instance_id) {
    for (uint8_t index = 0; index < MAX_VIEWS; ++index) if (!service->views[index].active) {
        RadarView* view = &service->views[index];
        clear_bytes(view, sizeof(*view));
        view->active = 1;
        view->instance_id = instance_id;
        return view;
    }
    return nullptr;
}

void draw_marker(const NativeExtensionHostApi* host, void* canvas, int32_t x, int32_t y, float heading) {
    const float angle = radians(heading);
    const float forward_x = host->math_sin(angle), forward_y = -host->math_cos(angle);
    const float side_x = -forward_y, side_y = forward_x;
    const int32_t tip_x = x + static_cast<int32_t>(forward_x * MARKER_TIP_PX);
    const int32_t tip_y = y + static_cast<int32_t>(forward_y * MARKER_TIP_PX);
    const int32_t left_x = x - static_cast<int32_t>(forward_x * MARKER_TAIL_PX) + static_cast<int32_t>(side_x * MARKER_HALF_WIDTH_PX);
    const int32_t left_y = y - static_cast<int32_t>(forward_y * MARKER_TAIL_PX) + static_cast<int32_t>(side_y * MARKER_HALF_WIDTH_PX);
    const int32_t right_x = x - static_cast<int32_t>(forward_x * MARKER_TAIL_PX) - static_cast<int32_t>(side_x * MARKER_HALF_WIDTH_PX);
    const int32_t right_y = y - static_cast<int32_t>(forward_y * MARKER_TAIL_PX) - static_cast<int32_t>(side_y * MARKER_HALF_WIDTH_PX);
    host->canvas_draw_line(canvas, tip_x, tip_y, left_x, left_y, 0xFFDF00, MARKER_LINE_WIDTH_PX);
    host->canvas_draw_line(canvas, left_x, left_y, right_x, right_y, 0xFFDF00, MARKER_LINE_WIDTH_PX);
    host->canvas_draw_line(canvas, right_x, right_y, tip_x, tip_y, 0xFFDF00, MARKER_LINE_WIDTH_PX);
}

void render_view(RadarService* service, RadarView* view) {
    const NativeExtensionHostApi* host = service->host;
    uint16_t count = 0;
    RadarConfig config = {};
    uint32_t version = 0;
    char status[72] = {};
    uint32_t refresh_ms = 0;
    if (!host->mutex_lock(service->mutex, 20)) return;
    version = service->version;
    if (version == view->rendered_version) { host->mutex_unlock(service->mutex); return; }
    count = service->count;
    config = service->config;
    refresh_ms = service->refresh_ms;
    copy_text(status, sizeof(status), service->status);
    for (uint16_t index = 0; index < count; ++index) service->render[index] = service->snapshot[index];
    host->mutex_unlock(service->mutex);

    const int32_t radar_height = view->height > FOOTER_HEIGHT_PX ? view->height - FOOTER_HEIGHT_PX : view->height;
    const int32_t center_x = view->width / 2;
    const int32_t center_y = radar_height / 2;
    const int32_t edge = (view->width < radar_height ? view->width : radar_height) / 2 - RADAR_INSET_PX;
    host->canvas_clear(view->canvas, 0x06131B);
    for (uint8_t ring = 1; ring <= 4; ++ring) host->canvas_draw_circle(view->canvas, center_x, center_y, edge * ring / 4, 0x1B5266, 1);
    host->canvas_draw_line(view->canvas, center_x, center_y - edge, center_x, center_y + edge, 0x1B5266, 1);
    host->canvas_draw_line(view->canvas, center_x - edge, center_y, center_x + edge, center_y, 0x1B5266, 1);
    host->canvas_draw_circle(view->canvas, center_x, center_y, 3, 0xFFFFFF, 1);

    for (uint8_t index = 0; index < MAX_LABELS; ++index) host->obj_set_hidden(view->plane_labels[index], true);
    for (uint16_t index = 0; index < count; ++index) {
        const Plane& plane = service->render[index];
        const float bearing = bearing_radians(host, config, plane);
        float radius = plane.distance_km / config.range_km * edge;
        if (plane.distance_km > 0.0f && radius < NEAR_HOME_MARKER_RADIUS_PX) {
            radius = NEAR_HOME_MARKER_RADIUS_PX;
        }
        const int32_t x = center_x + static_cast<int32_t>(host->math_sin(bearing) * radius);
        const int32_t y = center_y - static_cast<int32_t>(host->math_cos(bearing) * radius);
        draw_marker(host, view->canvas, x, y, plane.heading_degrees);
        if (index < MAX_LABELS) {
            char label[64] = {};
            append_text(label, sizeof(label), plane.callsign);
            append_text(label, sizeof(label), " ");
            append_fixed_1(label, sizeof(label), plane.distance_km);
            append_text(label, sizeof(label), "km ");
            if (plane.has_altitude) append_uint(label, sizeof(label), static_cast<uint32_t>(plane.altitude_feet));
            else append_text(label, sizeof(label), "-");
            append_text(label, sizeof(label), "ft ");
            append_text(label, sizeof(label), plane.aircraft_type);
            host->label_set_text(view->plane_labels[index], label);
            const float marker_angle = radians(plane.heading_degrees);
            const float marker_forward_x = host->math_sin(marker_angle);
            const float marker_forward_y = -host->math_cos(marker_angle);
            host->obj_set_pos(view->plane_labels[index],
                              x + static_cast<int32_t>(marker_forward_x * (MARKER_TIP_PX + MARKER_LABEL_GAP_PX)),
                              y + static_cast<int32_t>(marker_forward_y * (MARKER_TIP_PX + MARKER_LABEL_GAP_PX)) - 7);
            host->obj_set_hidden(view->plane_labels[index], false);
        }
    }
    char footer[96] = {};
    append_text(footer, sizeof(footer), status[0] ? status : "Waiting for ADS-B");
    append_text(footer, sizeof(footer), " | ");
    append_uint(footer, sizeof(footer), count);
    append_text(footer, sizeof(footer), " planes | ");
    append_uint(footer, sizeof(footer), refresh_ms);
    append_text(footer, sizeof(footer), "ms");
    host->label_set_text(view->status_label, footer);
    host->obj_set_pos(view->status_label, RADAR_INSET_PX, view->height - FOOTER_HEIGHT_PX + 5);
    view->rendered_version = version;
}

} // namespace

extern "C" void native_extension_create_instance(const NativeExtensionHostApi* host,
                                                  void* extension_context, uint32_t instance_id,
                                                  void* root, const char* config_json) {
    if (!host || host->abi_version != NATIVE_EXTENSION_ABI_VERSION || !root) return;
    host->log(NATIVE_EXTENSION_LOG_INFO, "flight radar: creating view");
    RadarService* service = get_service(host, extension_context);
    if (!service) {
        host->notify("Flight radar allocation failed");
        return;
    }
    RadarConfig config = {};
    if (!parse_config(config_json, &config)) {
        host->log(NATIVE_EXTENSION_LOG_WARN, "flight radar: invalid config");
        host->notify("Flight radar config invalid");
        return;
    }
    RadarView* view = create_view(service, instance_id);
    if (!view) {
        host->log(NATIVE_EXTENSION_LOG_WARN, "flight radar: view limit reached");
        host->notify("Flight radar has too many views");
        return;
    }

    view->width = static_cast<uint16_t>(host->obj_get_width(root));
    view->height = static_cast<uint16_t>(host->obj_get_height(root));
    if (view->width < 32 || view->height < 32) {
        char message[72] = "flight radar: view too small ";
        append_uint(message, sizeof(message), view->width);
        append_text(message, sizeof(message), "x");
        append_uint(message, sizeof(message), view->height);
        host->log(NATIVE_EXTENSION_LOG_WARN, message);
        view->active = 0;
        return;
    }
    view->canvas_buffer = host->alloc(host->canvas_buffer_size(view->width, view->height));
    view->canvas = host->canvas_create(root);
    if (!view->canvas_buffer || !view->canvas) {
        host->log(NATIVE_EXTENSION_LOG_ERROR, "flight radar: canvas allocation failed");
        if (view->canvas_buffer) host->free(view->canvas_buffer);
        view->active = 0;
        return;
    }
    host->canvas_set_buffer(view->canvas, view->canvas_buffer, view->width, view->height);
    host->obj_set_pos(view->canvas, 0, 0);
    host->obj_set_clickable(view->canvas, false);
    for (uint8_t index = 0; index < MAX_LABELS; ++index) {
        view->plane_labels[index] = host->label_create(root);
        host->obj_set_text_color(view->plane_labels[index], 0xD5F7FF);
        host->obj_set_hidden(view->plane_labels[index], true);
    }
    view->status_label = host->label_create(root);
    host->obj_set_text_color(view->status_label, 0x8AB7C7);
    host->label_set_text(view->status_label, "Starting ADS-B...");
    host->obj_set_pos(view->status_label, RADAR_INSET_PX, view->height - FOOTER_HEIGHT_PX + 5);

    if (host->mutex_lock(service->mutex, 100)) {
        if (service->active_views == 0) {
            service->config = config;
            set_status(service, "Starting ADS-B...");
            log_config(host, config);
        }
        ++service->active_views;
        if (!service->worker_started) {
            service->worker_started = host->task_create(radar_worker, "FlightRadar", 8192, service, 1, &service->worker_task) ? 1 : 0;
            if (!service->worker_started) {
                set_status(service, "Flight radar worker failed");
                host->log(NATIVE_EXTENSION_LOG_ERROR, "flight radar: worker creation failed");
            }
        }
        ++service->version;
        host->mutex_unlock(service->mutex);
    } else {
        host->log(NATIVE_EXTENSION_LOG_WARN, "flight radar: create mutex timeout");
    }
}

extern "C" void native_extension_destroy_instance(const NativeExtensionHostApi* host,
                                                   void* extension_context, uint32_t instance_id) {
    RadarService* service = host ? static_cast<RadarService*>(host->context_get_data(extension_context)) : nullptr;
    if (!service || !host->mutex_lock(service->mutex, 100)) return;
    for (uint8_t index = 0; index < MAX_VIEWS; ++index) {
        RadarView& view = service->views[index];
        if (!view.active || view.instance_id != instance_id) continue;
        if (view.canvas_buffer) host->free(view.canvas_buffer);
        clear_bytes(&view, sizeof(view));
        if (service->active_views) --service->active_views;
        break;
    }
    host->mutex_unlock(service->mutex);
}

extern "C" void native_extension_tick(const NativeExtensionHostApi* host,
                                       void* extension_context, uint32_t instance_id) {
    RadarService* service = host ? static_cast<RadarService*>(host->context_get_data(extension_context)) : nullptr;
    if (!service) return;
    for (uint8_t index = 0; index < MAX_VIEWS; ++index) {
        RadarView* view = &service->views[index];
        if (view->active && view->instance_id == instance_id) render_view(service, view);
    }
}