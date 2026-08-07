#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdio>

#include <ArduinoJson.h>

#include "binding_template.h"
#include "timer_binding.h"
#include "timer_engine.h"
#include "widgets/widget.h"

static unsigned long g_now = 0;

extern "C" unsigned long millis() {
    return g_now;
}

void action_list_dispatch(const ButtonAction*, uint8_t, const char*) {}

static void check_resolves(const char* token, const char* expected) {
    char output[64];
    binding_template_resolve(token, output, sizeof(output));
    assert(std::strcmp(output, expected) == 0);
}

int main() {
    timer_binding_init();

    check_resolves("[timer:1_target]", "0");
    assert(timer_configure_and_start(1, TIMER_MODE_DOWN, 125000, nullptr, 0));
    check_resolves("[timer:1_target]", "125");
    check_resolves("[timer:1_state]", "running");
    check_resolves("[timer:1_mode]", "down");
    check_resolves("[timer:1_expired]", "OFF");
    check_resolves("[timer:1]", "125.0");

    g_now = 5000;
    check_resolves("[timer:1_target]", "125");
    assert(std::fabs(resolve_number("[timer:1_target]", -1.0f) - 125.0f) < 0.001f);

    const char* valid_params[] = {"1", "1;ss", "1_state", "1_mode", "1_expired", "1_target"};
    for (const char* params : valid_params) {
        assert(binding_template_validate_params("timer", 5, params) == nullptr);
    }
    assert(binding_template_validate_params("timer", 5, "1_unknown") != nullptr);

    int timer_scheme = -1;
    for (uint8_t index = 0; index < binding_template_scheme_count(); index++) {
        if (std::strcmp(binding_template_scheme_name(index), "timer") == 0) {
            timer_scheme = index;
            break;
        }
    }
    assert(timer_scheme >= 0);
    JsonDocument metadata;
    JsonObject metadata_object = metadata.to<JsonObject>();
    assert(binding_template_describe_scheme((uint8_t)timer_scheme, &metadata_object));
    const char* keys = metadata_object["keys"] | "";
    assert(std::strstr(keys, "N=value") != nullptr);
    assert(std::strstr(keys, "N_state") != nullptr);
    assert(std::strstr(keys, "N_mode") != nullptr);
    assert(std::strstr(keys, "N_expired") != nullptr);
    assert(std::strstr(keys, "N_target") != nullptr);

    std::puts("timer_binding_target: PASS");
    return 0;
}