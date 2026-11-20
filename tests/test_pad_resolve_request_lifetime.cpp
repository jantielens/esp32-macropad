#include "main_loop_bridge.h"
#include "pad_binding.h"
#include "pad_resolve_request.h"

#include <freertos/semphr.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

static char g_seen_input[64] = {};
static int g_resolve_calls = 0;
static PadConfig g_effective_pad{};
static unsigned g_releases = 0;
static char g_seen_binding[CONFIG_LABEL_MAX_LEN] = {};

const PadConfig* pad_config_acquire(uint8_t page) { return page == 0 ? &g_effective_pad : nullptr; }
void pad_config_release(const PadConfig*) { ++g_releases; }
int pad_config_resolve_ref(const char* name, char*, size_t) {
    return !strcmp(name, "pad_0") ? 0 : -1;
}

extern "C" void pad_resolve(const char* const* inputs, size_t count,
                            const PadBinding* bindings, uint8_t binding_count,
                            char* out, size_t stride) {
    assert(count == 1);
    strlcpy(g_seen_input, inputs[0], sizeof(g_seen_input));
    strlcpy(out, "resolved", stride);
    if (binding_count) strlcpy(g_seen_binding, bindings[0].value, sizeof(g_seen_binding));
    g_resolve_calls++;
}

static void drain_on_wait(SemaphoreHandle_t, TickType_t ticks) {
    if (ticks) loop_bridge_drain();
}

static PadResolveStatus submit_timeout_request() {
    JsonDocument args_doc;
    args_doc["bindings"].add("[test:still-alive]");
    JsonDocument result_doc;
    const char* error = nullptr;
    return pad_resolve_request(args_doc.as<JsonObjectConst>(), result_doc.to<JsonObject>(), &error);
}

int main() {
    loop_bridge_init();
    assert(submit_timeout_request() == PAD_RESOLVE_ERROR);

    // The JSON request is gone; the deferred main-loop job must use its owned copy.
    loop_bridge_drain();
    assert(g_resolve_calls == 1);
    assert(strcmp(g_seen_input, "[test:still-alive]") == 0);

    test_freertos_semaphore_take_hook = drain_on_wait;
    assert(submit_timeout_request() == PAD_RESOLVE_OK);
    assert(g_resolve_calls == 2);
    JsonDocument args;
    args["button"]["icon_color"] = "[test:icon-color]";
    JsonDocument result;
    const char* error = nullptr;
    assert(pad_resolve_request(args.as<JsonObjectConst>(), result.to<JsonObject>(), &error) == PAD_RESOLVE_OK);
    assert(strcmp(g_seen_input, "[test:icon-color]") == 0);
    assert(g_resolve_calls == 3);
    g_effective_pad.binding_count = 1;
    strlcpy(g_effective_pad.bindings[0].name, "inherited", PAD_BINDING_NAME_MAX_LEN);
    strlcpy(g_effective_pad.bindings[0].value, "[pad:local]", CONFIG_LABEL_MAX_LEN);
    args.clear();
    args["screen"] = "pad_0";
    args["bindings"].add("[pad:inherited]");
    result.clear();
    assert(pad_resolve_request(args.as<JsonObjectConst>(), result.to<JsonObject>(), &error) == PAD_RESOLVE_OK);
    assert(g_releases == 1);
    assert(!strcmp(g_seen_binding, "[pad:local]"));
    puts("pad_resolve_request lifetime test passed");
    return 0;
}