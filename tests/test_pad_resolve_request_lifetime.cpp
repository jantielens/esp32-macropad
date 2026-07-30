#include "main_loop_bridge.h"
#include "pad_binding.h"
#include "pad_resolve_request.h"

#include <freertos/semphr.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

static char g_seen_input[64] = {};
static int g_resolve_calls = 0;

char* pad_config_read_raw(uint8_t, size_t*) { return nullptr; }
int pad_config_resolve_ref(const char*, char*, size_t) { return -1; }

extern "C" void pad_resolve(const char* const* inputs, size_t count,
                            const PadBinding*, uint8_t,
                            char* out, size_t stride) {
    assert(count == 1);
    strlcpy(g_seen_input, inputs[0], sizeof(g_seen_input));
    strlcpy(out, "resolved", stride);
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
    puts("pad_resolve_request lifetime test passed");
    return 0;
}