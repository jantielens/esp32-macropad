#include "binding_template.h"
#include "deferred_dispatch_slot.h"
#include "expr_binding.h"
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
static DeferredDispatchSlot<DISPLAY_TASK_DISPATCH_CTX_BYTES> g_display_slot;
static bool g_in_display_task = false;

DisplayTaskDispatchResult display_manager_dispatch(
    DisplayTaskExec exec, DisplayTaskCleanup cleanup, const void* ctx, size_t ctx_len,
    uint32_t timeout_ms, bool* out_ok, char* out_msg, size_t out_msg_len) {
    return g_display_slot.dispatch(exec, cleanup, ctx, ctx_len, timeout_ms, false,
                                   false, out_ok, out_msg, out_msg_len);
}

static void drain_display() {
    g_in_display_task = true;
    g_display_slot.drain();
    g_in_display_task = false;
}

const PadConfig* pad_config_acquire(uint8_t page) { return page == 0 ? &g_effective_pad : nullptr; }
void pad_config_release(const PadConfig*) { ++g_releases; }
int pad_config_resolve_ref(const char* name, char*, size_t) {
    return !strcmp(name, "pad_0") ? 0 : -1;
}

static BindingResolverStatus test_resolve(const char* params, char* out, size_t out_len) {
    assert(g_in_display_task);
    strlcpy(g_seen_input, params, sizeof(g_seen_input));
    strlcpy(out, "resolved", out_len);
    g_resolve_calls++;
    return BINDING_RESOLVER_RESOLVED;
}

extern "C" void mqtt_sub_store_ensure_binding_subscribed(const char*) {
    assert(g_in_display_task);
}

static void drain_on_wait(SemaphoreHandle_t, TickType_t ticks) {
    if (ticks) drain_display();
}

static PadResolveStatus submit_timeout_request() {
    JsonDocument args_doc;
    args_doc["bindings"].add("[test:still-alive]");
    JsonDocument result_doc;
    const char* error = nullptr;
    return pad_resolve_request(args_doc.as<JsonObjectConst>(), result_doc.to<JsonObject>(), &error);
}

int main() {
    assert(g_display_slot.init());
    const BindingSchemeSpec spec{1, 1, 1, -1, BINDING_VALIDATION_STANDARD, true};
    assert(binding_template_register("test", test_resolve, nullptr, spec));
    pad_binding_init();
    expr_binding_init();
    assert(submit_timeout_request() == PAD_RESOLVE_ERROR);
    assert(submit_timeout_request() == PAD_RESOLVE_BUSY);

    // The JSON request is gone; the deferred display job must use its owned copy.
    drain_display();
    assert(g_resolve_calls == 1);
    assert(strcmp(g_seen_input, "still-alive") == 0);

    test_freertos_semaphore_take_hook = drain_on_wait;
    assert(submit_timeout_request() == PAD_RESOLVE_OK);
    assert(g_resolve_calls == 2);
    JsonDocument args;
    args["button"]["icon_color"] = "[test:icon-color]";
    JsonDocument result;
    const char* error = nullptr;
    assert(pad_resolve_request(args.as<JsonObjectConst>(), result.to<JsonObject>(), &error) == PAD_RESOLVE_OK);
    assert(strcmp(g_seen_input, "icon-color") == 0);
    assert(g_resolve_calls == 3);
    g_effective_pad.binding_count = 2;
    strlcpy(g_effective_pad.bindings[0].name, "inherited", PAD_BINDING_NAME_MAX_LEN);
    strlcpy(g_effective_pad.bindings[0].value, "[pad:local]", CONFIG_LABEL_MAX_LEN);
    strlcpy(g_effective_pad.bindings[1].name, "local", PAD_BINDING_NAME_MAX_LEN);
    strlcpy(g_effective_pad.bindings[1].value, "[expr:5]", CONFIG_LABEL_MAX_LEN);
    args.clear();
    args["screen"] = "pad_0";
    args["bindings"].add("[pad:inherited]");
    result.clear();
    assert(pad_resolve_request(args.as<JsonObjectConst>(), result.to<JsonObject>(), &error) == PAD_RESOLVE_OK);
    assert(g_releases == 1);
    assert(result["resolved"][0]["value"] == "5");

    strlcpy(g_effective_pad.bindings[0].name, "doubled", PAD_BINDING_NAME_MAX_LEN);
    strlcpy(g_effective_pad.bindings[0].value, "[expr:[pad:base]*2]", CONFIG_LABEL_MAX_LEN);
    strlcpy(g_effective_pad.bindings[1].name, "base", PAD_BINDING_NAME_MAX_LEN);
    strlcpy(g_effective_pad.bindings[1].value, "[expr:21]", CONFIG_LABEL_MAX_LEN);
    args.clear();
    args["screen"] = "pad_0";
    args["bindings"].add("[pad:doubled]");
    args["bindings"].add("[pad:base]");
    args["button"]["label_center"] = "[pad:doubled]";
    result.clear();
    assert(pad_resolve_request(args.as<JsonObjectConst>(), result.to<JsonObject>(), &error) == PAD_RESOLVE_OK);
    assert(result["resolved"][0]["value"] == "42");
    assert(result["resolved"][1]["value"] == "21");
    assert(result["button"]["label_center"] == "42");
    args.remove("bindings");
    result.clear();
    assert(pad_resolve_request(args.as<JsonObjectConst>(), result.to<JsonObject>(), &error) == PAD_RESOLVE_OK);
    assert(result["button"]["label_center"] == "42");

    g_display_slot.beginShutdown();
    assert(g_display_slot.shutdown());
    assert(pad_resolve_request(args.as<JsonObjectConst>(), result.to<JsonObject>(), &error) == PAD_RESOLVE_ERROR);
    assert(error && !strcmp(error, "resolve unavailable"));
    puts("pad_resolve_request lifetime test passed");
    return 0;
}