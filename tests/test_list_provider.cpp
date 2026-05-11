// ============================================================================
// Unit tests for ListProvider registry and {id} substitution
// ============================================================================

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "list_provider.h"

static int g_pass = 0;
static int g_fail = 0;

#define TEST(name) static void test_##name()
#define RUN(name)  do { \
    printf("  %-50s ", #name); \
    test_##name(); \
    printf("PASS\n"); \
    g_pass++; \
} while(0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    assertion failed: %s\n", #cond); \
        g_fail++; return; \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("FAIL\n    %s: expected %d, got %d\n", #a, (int)(b), (int)(a)); \
        g_fail++; return; \
    } \
} while(0)

#define ASSERT_STR(field, expected) do { \
    if (strcmp((field), (expected)) != 0) { \
        printf("FAIL\n    %s: expected \"%s\", got \"%s\"\n", #field, (expected), (field)); \
        g_fail++; return; \
    } \
} while(0)

#define ASSERT_NULL(ptr) do { \
    if ((ptr) != nullptr) { \
        printf("FAIL\n    %s: expected nullptr\n", #ptr); \
        g_fail++; return; \
    } \
} while(0)

#define ASSERT_NOT_NULL(ptr) do { \
    if ((ptr) == nullptr) { \
        printf("FAIL\n    %s: expected non-null\n", #ptr); \
        g_fail++; return; \
    } \
} while(0)

// ---- Dummy providers for registry tests ----

static uint8_t dummy_provide_a(ListItem* items, uint8_t max) {
    (void)items; (void)max;
    return 0;
}

static uint8_t dummy_provide_b(ListItem* items, uint8_t max) {
    (void)items; (void)max;
    return 0;
}

static const ListProvider provider_a = { "alpha", "Alpha Provider", dummy_provide_a };
static const ListProvider provider_b = { "beta",  "Beta Provider",  dummy_provide_b };

// ============================================================================
// Registry Tests
// ============================================================================

TEST(register_and_find) {
    ASSERT_TRUE(list_provider_register(&provider_a));
    const ListProvider* found = list_provider_find("alpha");
    ASSERT_NOT_NULL(found);
    ASSERT_STR(found->id, "alpha");
    ASSERT_STR(found->title, "Alpha Provider");
}

TEST(find_second_provider) {
    ASSERT_TRUE(list_provider_register(&provider_b));
    const ListProvider* found = list_provider_find("beta");
    ASSERT_NOT_NULL(found);
    ASSERT_STR(found->id, "beta");
}

TEST(find_unknown_returns_null) {
    const ListProvider* found = list_provider_find("nonexistent");
    ASSERT_NULL(found);
}

TEST(find_null_returns_null) {
    const ListProvider* found = list_provider_find(nullptr);
    ASSERT_NULL(found);
}

TEST(register_full) {
    // Fill remaining slots (2 already registered: alpha, beta)
    static ListProvider extras[LIST_MAX_PROVIDERS];
    static char ids[LIST_MAX_PROVIDERS][16];
    int registered = 2; // alpha + beta already in
    for (int i = registered; i < LIST_MAX_PROVIDERS; i++) {
        snprintf(ids[i], sizeof(ids[i]), "prov_%d", i);
        extras[i].id = ids[i];
        extras[i].title = ids[i];
        extras[i].provide = dummy_provide_a;
        ASSERT_TRUE(list_provider_register(&extras[i]));
    }
    // Registry is now full — next register should fail
    static const ListProvider overflow = { "overflow", "Overflow", dummy_provide_a };
    ASSERT_TRUE(!list_provider_register(&overflow));
}

TEST(register_null_returns_false) {
    // Even though registry is full, null should be rejected
    ASSERT_TRUE(!list_provider_register(nullptr));
}

// ============================================================================
// {id} Substitution Tests
// ============================================================================

TEST(substitute_simple) {
    char buf[64] = "value={id}";
    list_substitute_id_in_field(buf, sizeof(buf), "test-1");
    ASSERT_STR(buf, "value=test-1");
}

TEST(substitute_multiple) {
    char buf[64] = "{id}:{id}";
    list_substitute_id_in_field(buf, sizeof(buf), "x");
    ASSERT_STR(buf, "x:x");
}

TEST(substitute_no_token) {
    char buf[64] = "no token here";
    list_substitute_id_in_field(buf, sizeof(buf), "anything");
    ASSERT_STR(buf, "no token here");
}

TEST(substitute_empty_id) {
    char buf[64] = "prefix-{id}-suffix";
    list_substitute_id_in_field(buf, sizeof(buf), "");
    ASSERT_STR(buf, "prefix--suffix");
}

TEST(substitute_longer_id) {
    char buf[64] = "id={id}!";
    list_substitute_id_in_field(buf, sizeof(buf), "long-identifier");
    ASSERT_STR(buf, "id=long-identifier!");
}

TEST(substitute_overflow_protection) {
    char buf[16] = "{id}";
    // "abcdefghijklmnop" is 16 chars — with null terminator would need 17 bytes,
    // which exceeds buf size. Substitution should be skipped.
    list_substitute_id_in_field(buf, sizeof(buf), "abcdefghijklmnop");
    // Original should be preserved since replacement doesn't fit
    ASSERT_STR(buf, "{id}");
}

TEST(substitute_exact_fit) {
    char buf[16] = "{id}";
    // "123456789ab" is 11 chars. Replacing 4-char token: need 11+1=12 bytes. Fits in 16.
    list_substitute_id_in_field(buf, sizeof(buf), "123456789ab");
    ASSERT_STR(buf, "123456789ab");
}

TEST(substitute_in_middle) {
    char buf[64] = "start-{id}-end";
    list_substitute_id_in_field(buf, sizeof(buf), "MID");
    ASSERT_STR(buf, "start-MID-end");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("ListProvider registry tests:\n");
    RUN(register_and_find);
    RUN(find_second_provider);
    RUN(find_unknown_returns_null);
    RUN(find_null_returns_null);
    RUN(register_full);
    RUN(register_null_returns_false);

    printf("\n{id} substitution tests:\n");
    RUN(substitute_simple);
    RUN(substitute_multiple);
    RUN(substitute_no_token);
    RUN(substitute_empty_id);
    RUN(substitute_longer_id);
    RUN(substitute_overflow_protection);
    RUN(substitute_exact_fit);
    RUN(substitute_in_middle);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
