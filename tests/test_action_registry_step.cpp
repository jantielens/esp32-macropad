// ============================================================================
// Guard test for the action-type registry value-field contract
// ============================================================================
// Device-class action types store their value in the opaque
// ActionPayload::device_class arm, invisible to shared firmware at compile
// time. Three registry hooks reach that field: resolve_bindings, has_binding,
// and substitute_step. A device class that forgets to wire substitute_step
// silently regresses the numeric rocker {step} feature (the bug that motivated
// this test).
//
// This test registers a SYNTHETIC device-class value-action — mirroring the
// canonical `{ char command[]; char value[]; }` payload shape — and asserts
// that all three hooks behave correctly when invoked through the registry
// (action_type_find -> def->hook). It is intentionally decoupled from any one
// device class so it keeps guarding the contract even as device classes come
// and go.

#include <cstdio>
#include <cstring>

#include "action_registry.h"
#include "binding_template.h"
#include "pad_config.h"

// ---------------------------------------------------------------------------
// Synthetic device-class payload — same shape every device class uses.
// ---------------------------------------------------------------------------
#define ACTION_TYPE_FAKE "fake"

struct FakePayload {
    char command[16];
    char value[32];
};
static_assert(sizeof(FakePayload) <= ACTION_PAYLOAD_DEVICE_CLASS_BYTES,
              "FakePayload exceeds device_class arm");

static FakePayload& fake_payload(ButtonAction& act) {
    return *reinterpret_cast<FakePayload*>(act.payload.device_class);
}
static const FakePayload& fake_payload(const ButtonAction& act) {
    return *reinterpret_cast<const FakePayload*>(act.payload.device_class);
}

// ---------------------------------------------------------------------------
// Hook implementations — mirror the device-class pattern exactly.
// ---------------------------------------------------------------------------
static void fake_resolve_bindings(ButtonAction& act) {
    char* field = fake_payload(act).value;
    if (field[0] && binding_template_has_bindings(field)) {
        char tmp[BINDING_TEMPLATE_MAX_LEN];
        binding_template_resolve(field, tmp, sizeof(tmp));
        strlcpy(field, tmp, sizeof(fake_payload(act).value));
    }
}

static bool fake_has_binding(const ButtonAction& act) {
    const char* f = fake_payload(act).value;
    return f[0] && memchr(f, '[', strlen(f)) != nullptr;
}

static void fake_substitute_step(ButtonAction& act, float step) {
    FakePayload& p = fake_payload(act);
    action_substitute_step_field(p.value, sizeof(p.value), step);
}

static const ActionTypeDef fake_action_type = {
    /* type_name        */ ACTION_TYPE_FAKE,
    /* parse            */ nullptr,
    /* serialize        */ nullptr,
    /* resolve_bindings */ fake_resolve_bindings,
    /* has_binding      */ fake_has_binding,
    /* dispatch         */ nullptr,
    /* substitute_step  */ fake_substitute_step,
};

// ---------------------------------------------------------------------------
// Mock binding resolver
// ---------------------------------------------------------------------------
static bool mock_resolve(const char* params, char* out, size_t out_len) {
    (void)params;
    strlcpy(out, "RESOLVED", out_len);
    return true;
}
static void mock_collect(const char* params, void* user_data) {
    (void)params;
    (void)user_data;
}

// ---------------------------------------------------------------------------
// Harness
// ---------------------------------------------------------------------------
static int g_pass = 0;
static int g_fail = 0;

static void check_str(const char* got, const char* expected, const char* label) {
    if (strcmp(got, expected) != 0) {
        printf("  FAIL [%s]\n    got:      \"%s\"\n    expected: \"%s\"\n", label, got, expected);
        g_fail++;
        return;
    }
    g_pass++;
}
static void check_true(bool cond, const char* label) {
    if (!cond) {
        printf("  FAIL [%s]\n", label);
        g_fail++;
        return;
    }
    g_pass++;
}

static ButtonAction make_fake(const char* value) {
    ButtonAction act;
    memset(&act, 0, sizeof(act));
    strlcpy(act.type, ACTION_TYPE_FAKE, sizeof(act.type));
    FakePayload& p = fake_payload(act);
    strlcpy(p.command, "adjust", sizeof(p.command));
    strlcpy(p.value, value, sizeof(p.value));
    return act;
}

// ---------------------------------------------------------------------------
// Tests — all hooks reached through the registry, never directly.
// ---------------------------------------------------------------------------
static void test_lookup_returns_registered_def() {
    printf("--- registry returns the registered def ---\n");
    const ActionTypeDef* def = action_type_find(ACTION_TYPE_FAKE);
    check_true(def != nullptr, "fake type is registered");
    check_true(def && def->substitute_step != nullptr, "substitute_step hook present");
    check_true(def && def->resolve_bindings != nullptr, "resolve_bindings hook present");
    check_true(def && def->has_binding != nullptr, "has_binding hook present");
}

static void test_substitute_step_via_registry() {
    printf("--- substitute_step substitutes {step} via registry ---\n");
    const ActionTypeDef* def = action_type_find(ACTION_TYPE_FAKE);
    if (!def || !def->substitute_step) { g_fail++; return; }
    {
        ButtonAction act = make_fake("adjust_base {step}");
        def->substitute_step(act, 5.0f);
        check_str(fake_payload(act).value, "adjust_base 5", "positive step substituted");
    }
    {
        ButtonAction act = make_fake("adjust_base {step}");
        def->substitute_step(act, -2.5f);
        check_str(fake_payload(act).value, "adjust_base -2.5", "negative fractional step substituted");
    }
    {
        ButtonAction act = make_fake("no token here");
        def->substitute_step(act, 9.0f);
        check_str(fake_payload(act).value, "no token here", "field without token unchanged");
    }
}

static void test_has_binding_via_registry() {
    printf("--- has_binding detects bindings via registry ---\n");
    const ActionTypeDef* def = action_type_find(ACTION_TYPE_FAKE);
    if (!def || !def->has_binding) { g_fail++; return; }
    {
        ButtonAction act = make_fake("[mock:x]");
        check_true(def->has_binding(act), "binding token detected");
    }
    {
        ButtonAction act = make_fake("plain");
        check_true(!def->has_binding(act), "plain value reports no binding");
    }
    {
        ButtonAction act = make_fake("");
        check_true(!def->has_binding(act), "empty value reports no binding");
    }
}

static void test_resolve_bindings_via_registry() {
    printf("--- resolve_bindings resolves value via registry ---\n");
    const ActionTypeDef* def = action_type_find(ACTION_TYPE_FAKE);
    if (!def || !def->resolve_bindings) { g_fail++; return; }
    {
        ButtonAction act = make_fake("[mock:x]");
        def->resolve_bindings(act);
        check_str(fake_payload(act).value, "RESOLVED", "binding resolved");
    }
    {
        // resolve_bindings must leave {step} untouched — only the rocker
        // substitutes it. This is the cross-feature invariant.
        ButtonAction act = make_fake("{step}");
        def->resolve_bindings(act);
        check_str(fake_payload(act).value, "{step}", "{step} survives binding resolution");
    }
}

int main() {
    printf("=== action registry value-field contract tests ===\n\n");

    binding_template_register("mock", mock_resolve, mock_collect);
    action_type_register(&fake_action_type);

    test_lookup_returns_registered_def();
    test_substitute_step_via_registry();
    test_has_binding_via_registry();
    test_resolve_bindings_via_registry();

    printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
