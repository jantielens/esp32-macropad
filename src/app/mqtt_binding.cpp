#include "mqtt_binding.h"

#if HAS_MQTT && HAS_DISPLAY

#include "binding_template.h"
#include "mqtt_sub_store.h"
#include "pad_config.h"

#include <string.h>

// Splits "topic;path;format" on the first two ';'; the format may contain ';'.
static void parse_mqtt_params(const char* params,
                              char* topic, size_t topic_len,
                              char* path, size_t path_len,
                              char* fmt, size_t fmt_len) {
    topic[0] = '\0';
    path[0] = '\0';
    fmt[0] = '\0';
    if (!params || !params[0]) return;

    const char* s1 = strchr(params, ';');
    if (!s1) {
        strlcpy(topic, params, topic_len);
        return;
    }
    size_t tlen = (size_t)(s1 - params);
    if (tlen >= topic_len) tlen = topic_len - 1;
    memcpy(topic, params, tlen);
    topic[tlen] = '\0';

    const char* s2 = strchr(s1 + 1, ';');
    if (!s2) {
        strlcpy(path, s1 + 1, path_len);
        return;
    }
    size_t plen = (size_t)(s2 - (s1 + 1));
    if (plen >= path_len) plen = path_len - 1;
    memcpy(path, s1 + 1, plen);
    path[plen] = '\0';

    strlcpy(fmt, s2 + 1, fmt_len);
}

static BindingResolverStatus mqtt_binding_resolve(const char* params, char* out, size_t out_len) {
    char topic[CONFIG_MQTT_TOPIC_MAX_LEN];
    char path[CONFIG_JSON_PATH_MAX_LEN];
    char fmt[CONFIG_FORMAT_MAX_LEN];

    parse_mqtt_params(params, topic, sizeof(topic), path, sizeof(path), fmt, sizeof(fmt));

    if (!topic[0]) {
        strlcpy(out, "ERR:no topic", out_len);
        return BINDING_RESOLVER_UNAVAILABLE;
    }

    static char payload[MQTT_SUB_STORE_MAX_VALUE_LEN];
    bool truncated = false;
    if (!mqtt_sub_store_get(topic, payload, sizeof(payload), nullptr, &truncated)) {
        return BINDING_RESOLVER_UNAVAILABLE;
    }

    const char* jp = (path[0]) ? path : ".";
    char extracted[128];
    if (!mqtt_sub_store_extract_json(payload, jp, extracted, sizeof(extracted))) {
        strlcpy(extracted, truncated ? "ERR:too big" : payload, sizeof(extracted));
    }

    if (fmt[0]) {
        mqtt_sub_store_format_value(extracted, fmt, out, out_len);
    } else {
        strlcpy(out, extracted, out_len);
    }
    return BINDING_RESOLVER_RESOLVED;
}

static void mqtt_binding_collect(const char* params, void* user_data) {
    char topic[CONFIG_MQTT_TOPIC_MAX_LEN];
    char path[CONFIG_JSON_PATH_MAX_LEN];
    char fmt[CONFIG_FORMAT_MAX_LEN];

    parse_mqtt_params(params, topic, sizeof(topic), path, sizeof(path), fmt, sizeof(fmt));
    if (topic[0] && user_data) mqtt_sub_store_collect_topic(user_data, topic);
}

static const char* mqtt_binding_status() {
    return mqtt_sub_store_active() ? nullptr : "No MQTT broker configured";
}

static const BindingParamDoc kMqttParams[] = {
    {"topic", "MQTT topic to subscribe to."},
    {"path", "JSON field such as temperature or a.b; empty or . for the raw payload."},
    {"format", "printf format: %d, %.1f, %s, with units inside, e.g. %.1f °C."},
};

static const BindingExampleDoc kMqttExamples[] = {
    {"[mqtt:sensors/temp]", "Raw payload of a topic."},
    {"[mqtt:sensors/state;temperature;%.1f °C]", "JSON field formatted with a unit."},
    {"[mqtt:solar/power;watts|0]", "Show 0 until the first message arrives."},
    {"Power: [mqtt:grid;power;%d] W", "Text around a token is kept."},
};

static const BindingSchemeDoc kMqttDoc = {
    "Data", "Live values from MQTT topics, optionally from a JSON field.",
    BINDING_DOC_LIST(kMqttParams), BINDING_DOC_NONE, BINDING_DOC_LIST(kMqttExamples),
    nullptr, false, BINDING_DOC_NONE,
    "Topics are subscribed when the pad is saved. Values show --- until the first message.",
    mqtt_binding_status,
};

void mqtt_binding_init() {
    binding_template_register("mqtt", mqtt_binding_resolve, mqtt_binding_collect,
                              {1, 3, 2, 2, BINDING_VALIDATION_STANDARD, true, nullptr, nullptr,
                               &kMqttDoc});
}

#else

void mqtt_binding_init() {}

#endif
