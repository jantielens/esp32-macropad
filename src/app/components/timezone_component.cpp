#include "component_registry.h"
#include "time_service.h"
#include "web_portal_json.h"

namespace {
void timezone_catalog(AsyncWebServerRequest* request) {
    auto document = make_psram_json_doc(12288);
    JsonArray cities = document->createNestedArray("cities");
    size_t count = 0;
    const TimezoneEntry* entries = time_service_timezones(&count);
    for (size_t index = 0; index < count; ++index) {
        JsonObject city = cities.createNestedObject();
        city["name"] = entries[index].name;
        city["posix"] = entries[index].posix;
    }
    web_portal_send_json_chunked(request, document);
}
void timezone_preview(AsyncWebServerRequest* request) {
    const String timezone = request->hasParam("timezone") ? request->getParam("timezone")->value() : String();
    if (!time_service_timezone_valid(timezone.c_str())) {
        web_portal_send_json_error(request, 400, "Invalid timezone");
        return;
    }
    const time_t epoch = time(nullptr);
    char local_time[32], offset[8];
    if (!time_service_format(epoch, "%Y-%m-%d %H:%M:%S", timezone.c_str(), local_time, sizeof(local_time))
        || !time_service_format(epoch, "%z", timezone.c_str(), offset, sizeof(offset))) {
        web_portal_send_json_error(request, 503, "Device time unavailable");
        return;
    }
    auto document = make_psram_json_doc(1024);
    (*document)["epoch"] = static_cast<int64_t>(epoch);
    (*document)["local_time"] = local_time;
    (*document)["utc_offset"] = offset;
    (*document)["ready"] = time_service_ready();
    web_portal_send_json_chunked(request, document);
}
const ComponentAction timezone_actions[] = {
    {"catalog", HTTP_GET, timezone_catalog, nullptr},
    {"preview", HTTP_GET, timezone_preview, nullptr},
};
}
static ComponentDef timezone_component = {
    .id = "timezone",
    .category = "device",
    .display_name = "Timezone",
    .nav_order = 25,
    .custom_actions = timezone_actions,
    .num_custom_actions = sizeof(timezone_actions) / sizeof(timezone_actions[0]),
    .fragment_id = "timezone",
};
REGISTER_COMPONENT(timezone);