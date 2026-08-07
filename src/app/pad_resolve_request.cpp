// ============================================================================
// pad_resolve_request.cpp — see pad_resolve_request.h.
//
// Compiled directly (sketch root). Gated HAS_DISPLAY && HAS_MQTT (bindings are
// meaningless without the resolver schemes). Independent of HAS_MCP so the web
// portal endpoint works even when MCP is compiled out.
// ============================================================================

#include "pad_resolve_request.h"

#if HAS_DISPLAY && HAS_MQTT

#include "binding_template.h"   // BINDING_TEMPLATE_MAX_LEN
#include "main_loop_bridge.h"
#include "pad_binding.h"        // pad_resolve()
#include "pad_config.h"         // PadBinding, pad_config_read_raw, pad_config_resolve_ref
#include "psram_json_allocator.h"

#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <string.h>

static constexpr size_t   RESOLVE_MAX        = 32;
static constexpr size_t   RESOLVE_STRIDE     = BINDING_TEMPLATE_MAX_LEN;
static constexpr uint32_t RESOLVE_TIMEOUT_MS = 2000;

// Bindable button fields resolved for the `button` form (labels/colors/state +
// widget data bindings). Names match set_button.
static const char* const kResolveButtonFields[] = {
    "label_top", "label_center", "label_bottom",
    "bg_color", "fg_color", "border_color", "border_width", "corner_radius", "btn_state",
    "widget_data_binding", "widget_data_binding_2",
    "widget_data_binding_3", "widget_data_binding_4",
};

struct ResolvePayload {
    char*       out;
    PadBinding* binds;
    uint8_t     bind_count;
    size_t      count;
    size_t      stride;
    const char* inputs[RESOLVE_MAX];
    char        input_bytes[1];
};

struct ResolveCtx {
    ResolvePayload* payload;
};

// Runs on the main loop (LVGL task context) via the bridge.
static void exec_resolve(const void* ctx, bool* ok, char* msg, size_t msg_len) {
    const ResolveCtx* c = (const ResolveCtx*)ctx;
    ResolvePayload* payload = c ? c->payload : nullptr;
    if (!payload) {
        *ok = false;
        strlcpy(msg, "invalid resolve request", msg_len);
        return;
    }
    pad_resolve(payload->inputs, payload->count, payload->binds,
                payload->bind_count, payload->out, payload->stride);
    *ok = true;
    snprintf(msg, msg_len, "resolved %u", (unsigned)payload->count);
}

// PSRAM-preferred allocation with internal-RAM fallback (no HAS_MCP dependency).
static void* presolve_alloc(size_t n) {
    void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return p;
}

static void resolve_payload_free(ResolvePayload* payload) {
    if (!payload) return;
    if (payload->out) heap_caps_free(payload->out);
    if (payload->binds) heap_caps_free(payload->binds);
    heap_caps_free(payload);
}

static void cleanup_abandoned_resolve(const void* ctx) {
    const ResolveCtx* resolve = (const ResolveCtx*)ctx;
    resolve_payload_free(resolve ? resolve->payload : nullptr);
}

// Copy a pad's stored [pad:name] bindings into a heap PadBinding[]. Returns the
// count and sets *out_binds (caller frees), or 0 with *out_binds == nullptr.
static uint8_t load_pad_bindings(uint8_t page, PadBinding** out_binds) {
    *out_binds = nullptr;
    size_t len = 0;
    char* raw = pad_config_read_raw(page, &len);
    if (!raw) return 0;
    BasicJsonDocument<PsramJsonAllocator> doc(len + 512);
    DeserializationError e = deserializeJson(doc, raw, len);
    free(raw);
    if (e) return 0;
    JsonObjectConst pb = doc["bindings"];
    if (pb.isNull() || pb.size() == 0) return 0;
    PadBinding* arr = (PadBinding*)presolve_alloc(sizeof(PadBinding) * PAD_MAX_BINDINGS);
    if (!arr) return 0;
    uint8_t n = 0;
    for (JsonPairConst kv : pb) {
        if (n >= PAD_MAX_BINDINGS) break;
        strlcpy(arr[n].name, kv.key().c_str(), PAD_BINDING_NAME_MAX_LEN);
        const char* v = kv.value().as<const char*>();
        strlcpy(arr[n].value, v ? v : "", CONFIG_LABEL_MAX_LEN);
        n++;
    }
    *out_binds = arr;
    return n;
}

PadResolveStatus pad_resolve_request(JsonObjectConst args, JsonObject result,
                                     const char** err_msg) {
    auto set_err = [&](const char* m) { if (err_msg) *err_msg = m; };
    set_err(nullptr);

    JsonArrayConst  in_bindings = args["bindings"];
    JsonObjectConst in_button   = args["button"];
    const bool has_bindings = !in_bindings.isNull() && in_bindings.size() > 0;
    const bool has_button   = !in_button.isNull();
    if (!has_bindings && !has_button) {
        set_err("provide 'bindings' (array of strings) and/or 'button' (object)");
        return PAD_RESOLVE_BAD_PARAMS;
    }

    // Optional pad context so [pad:name] tokens resolve for the given pad.
    PadBinding* binds = nullptr;
    uint8_t bind_count = 0;
    const char* screen = args["screen"] | "";
    if (screen[0]) {
        char eb[64];
        int pg = pad_config_resolve_ref(screen, eb, sizeof(eb));
        if (pg < 0) { set_err("unknown screen"); return PAD_RESOLVE_BAD_PARAMS; }
        bind_count = load_pad_bindings((uint8_t)pg, &binds);
    }

    // Collect input template strings before copying them into payload-owned
    // storage for deferred execution.
    const char* inputs[RESOLVE_MAX];
    const char* field_names[RESOLVE_MAX];  // non-null => came from a button field
    size_t count = 0;
    if (has_bindings) {
        for (JsonVariantConst v : in_bindings) {
            if (count >= RESOLVE_MAX) break;
            const char* s = v.as<const char*>();
            if (!s) continue;
            inputs[count] = s; field_names[count] = nullptr; count++;
        }
    }
    const size_t button_start = count;
    if (has_button) {
        for (const char* f : kResolveButtonFields) {
            if (count >= RESOLVE_MAX) break;
            const char* s = in_button[f].as<const char*>();
            if (!s || !s[0]) continue;
            inputs[count] = s; field_names[count] = f; count++;
        }
    }
    if (count == 0) {
        if (binds) heap_caps_free(binds);
        set_err("no resolvable string fields provided");
        return PAD_RESOLVE_BAD_PARAMS;
    }

    size_t input_bytes = 0;
    for (size_t i = 0; i < count; ++i) input_bytes += strlen(inputs[i]) + 1;
    const size_t payload_bytes = sizeof(ResolvePayload) - 1 + input_bytes;
    ResolvePayload* payload = (ResolvePayload*)presolve_alloc(payload_bytes);
    if (!payload) {
        if (binds) heap_caps_free(binds);
        set_err("out of memory");
        return PAD_RESOLVE_OOM;
    }
    memset(payload, 0, payload_bytes);
    payload->binds = binds;
    payload->bind_count = bind_count;
    payload->count = count;
    payload->stride = RESOLVE_STRIDE;
    char* input = payload->input_bytes;
    for (size_t i = 0; i < count; ++i) {
        const size_t len = strlen(inputs[i]) + 1;
        memcpy(input, inputs[i], len);
        payload->inputs[i] = input;
        input += len;
    }

    payload->out = (char*)presolve_alloc(count * RESOLVE_STRIDE);
    if (!payload->out) {
        resolve_payload_free(payload);
        set_err("out of memory");
        return PAD_RESOLVE_OOM;
    }

    ResolveCtx ctx = { payload };
    bool ok = false; char msg[160] = {0};
    LoopBridgeResult r = loop_bridge_dispatch(exec_resolve, &ctx, sizeof(ctx),
                                              RESOLVE_TIMEOUT_MS, &ok, msg, sizeof(msg),
                                              cleanup_abandoned_resolve);
    if (r == LOOP_BRIDGE_BUSY) {
        resolve_payload_free(payload);
        set_err("busy, retry");
        return PAD_RESOLVE_BUSY;
    }
    if (r != LOOP_BRIDGE_OK) {
        if (r != LOOP_BRIDGE_TIMEOUT) resolve_payload_free(payload);
        set_err(r == LOOP_BRIDGE_TIMEOUT ? "resolve timed out" : "resolve unavailable");
        return PAD_RESOLVE_ERROR;
    }

    // Success — copy resolved values into the result (String forces ArduinoJson
    // to duplicate the bytes; `out` is freed before the response serializes).
    if (has_bindings) {
        JsonArray arr = result.createNestedArray("resolved");
        for (size_t i = 0; i < button_start; i++) {
            JsonObject o = arr.createNestedObject();
            o["input"] = String(payload->inputs[i]);
            o["value"] = String(payload->out + i * RESOLVE_STRIDE);
        }
    }
    if (has_button) {
        JsonObject bo = result.createNestedObject("button");
        for (size_t i = button_start; i < count; i++) {
            bo[field_names[i]] = String(payload->out + i * RESOLVE_STRIDE);
        }
    }
    resolve_payload_free(payload);
    return PAD_RESOLVE_OK;
}

#endif // HAS_DISPLAY && HAS_MQTT
