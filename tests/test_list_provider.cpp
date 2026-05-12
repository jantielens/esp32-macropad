// ============================================================================
// Unit tests for ListProvider registry and [list:provider_id.selected] binding
// ============================================================================

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "list_provider.h"
#include "list_binding.h"
#include "binding_template.h"

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
// [list:provider_id.selected] Binding Tests
// ============================================================================

TEST(binding_resolve_scoped) {
    list_binding_init();
    list_binding_set_selected("pads", "pad_3");
    char buf[64];
    bool ok = binding_template_resolve("[list:pads.selected]", buf, sizeof(buf));
    ASSERT_TRUE(ok);
    ASSERT_STR(buf, "pad_3");
}

TEST(binding_resolve_different_provider) {
    list_binding_set_selected("shutter_tests", "test_7");
    char buf[64];
    bool ok = binding_template_resolve("[list:shutter_tests.selected]", buf, sizeof(buf));
    ASSERT_TRUE(ok);
    ASSERT_STR(buf, "test_7");
}

TEST(binding_providers_independent) {
    // "pads" should still have its own selection, not affected by "shutter_tests"
    char buf[64];
    bool ok = binding_template_resolve("[list:pads.selected]", buf, sizeof(buf));
    ASSERT_TRUE(ok);
    ASSERT_STR(buf, "pad_3");
}

TEST(binding_resolve_empty_returns_placeholder) {
    list_binding_set_selected("empty_prov", "");
    char buf[64];
    binding_template_resolve("[list:empty_prov.selected]", buf, sizeof(buf));
    ASSERT_STR(buf, "---");
}

TEST(binding_resolve_null_item_returns_placeholder) {
    list_binding_set_selected("null_prov", nullptr);
    char buf[64];
    binding_template_resolve("[list:null_prov.selected]", buf, sizeof(buf));
    ASSERT_STR(buf, "---");
}

TEST(binding_unknown_provider_returns_placeholder) {
    char buf[64];
    binding_template_resolve("[list:unknown.selected]", buf, sizeof(buf));
    ASSERT_STR(buf, "---");
}

TEST(binding_no_dot_returns_placeholder) {
    char buf[64];
    binding_template_resolve("[list:selected]", buf, sizeof(buf));
    ASSERT_STR(buf, "---");
}

TEST(binding_bad_key_returns_placeholder) {
    char buf[64];
    binding_template_resolve("[list:pads.count]", buf, sizeof(buf));
    ASSERT_STR(buf, "---");
}

TEST(binding_in_template) {
    char buf[64];
    binding_template_resolve("go to [list:pads.selected]!", buf, sizeof(buf));
    ASSERT_STR(buf, "go to pad_3!");
}

// ============================================================================
// Main
// ============================================================================

// ============================================================================
// list_filter_items tests
// ============================================================================

static void make_items(ListItem* items, uint8_t n) {
    static const char* labels[] = { "Home", "Office", "Garage", "Garden", "Bedroom", "Kitchen" };
    for (uint8_t i = 0; i < n; i++) {
        snprintf(items[i].id, LIST_ITEM_ID_MAX, "pad_%u", i);
        snprintf(items[i].label, LIST_ITEM_LABEL_MAX, "%s", labels[i % 6]);
    }
}

TEST(filter_empty_passes_all) {
    ListItem items[6]; make_items(items, 6);
    ASSERT_EQ(list_filter_items(items, 6, ""), 6);
}

TEST(filter_null_passes_all) {
    ListItem items[6]; make_items(items, 6);
    ASSERT_EQ(list_filter_items(items, 6, nullptr), 6);
}

TEST(filter_star_passes_all) {
    ListItem items[6]; make_items(items, 6);
    ASSERT_EQ(list_filter_items(items, 6, "*"), 6);
}

TEST(filter_exact_id_match) {
    ListItem items[6]; make_items(items, 6);
    uint8_t n = list_filter_items(items, 6, "pad_3");
    ASSERT_EQ(n, 1);
    ASSERT_STR(items[0].id, "pad_3");
}

TEST(filter_glob_id_prefix) {
    ListItem items[6]; make_items(items, 6);
    ASSERT_EQ(list_filter_items(items, 6, "pad_*"), 6);
}

TEST(filter_glob_label_substring) {
    ListItem items[6]; make_items(items, 6);
    uint8_t n = list_filter_items(items, 6, "*Gar*");
    ASSERT_EQ(n, 2);
    ASSERT_STR(items[0].label, "Garage");
    ASSERT_STR(items[1].label, "Garden");
}

TEST(filter_index_single) {
    ListItem items[6]; make_items(items, 6);
    uint8_t n = list_filter_items(items, 6, "#2");
    ASSERT_EQ(n, 1);
    ASSERT_STR(items[0].id, "pad_2");
}

TEST(filter_index_range) {
    ListItem items[6]; make_items(items, 6);
    uint8_t n = list_filter_items(items, 6, "#1-3");
    ASSERT_EQ(n, 3);
    ASSERT_STR(items[0].id, "pad_1");
    ASSERT_STR(items[2].id, "pad_3");
}

TEST(filter_exclusion_only) {
    ListItem items[6]; make_items(items, 6);
    uint8_t n = list_filter_items(items, 6, "!pad_0");
    ASSERT_EQ(n, 5);
    ASSERT_STR(items[0].id, "pad_1");
}

TEST(filter_mixed_include_exclude) {
    ListItem items[6]; make_items(items, 6);
    uint8_t n = list_filter_items(items, 6, "pad_*,!pad_5");
    ASSERT_EQ(n, 5);
    ASSERT_STR(items[4].id, "pad_4");
}

TEST(filter_case_insensitive) {
    ListItem items[6]; make_items(items, 6);
    uint8_t n = list_filter_items(items, 6, "home");
    ASSERT_EQ(n, 1);
    ASSERT_STR(items[0].label, "Home");
}

TEST(filter_no_match_returns_zero) {
    ListItem items[6]; make_items(items, 6);
    ASSERT_EQ(list_filter_items(items, 6, "nonexistent"), 0);
}

TEST(filter_multi_index_rules) {
    ListItem items[6]; make_items(items, 6);
    uint8_t n = list_filter_items(items, 6, "#0,#2,#4");
    ASSERT_EQ(n, 3);
    ASSERT_STR(items[0].id, "pad_0");
    ASSERT_STR(items[1].id, "pad_2");
    ASSERT_STR(items[2].id, "pad_4");
}

int main() {
    printf("ListProvider registry tests:\n");
    RUN(register_and_find);
    RUN(find_second_provider);
    RUN(find_unknown_returns_null);
    RUN(find_null_returns_null);
    RUN(register_full);
    RUN(register_null_returns_false);

    printf("\n[list:provider_id.selected] binding tests:\n");
    RUN(binding_resolve_scoped);
    RUN(binding_resolve_different_provider);
    RUN(binding_providers_independent);
    RUN(binding_resolve_empty_returns_placeholder);
    RUN(binding_resolve_null_item_returns_placeholder);
    RUN(binding_unknown_provider_returns_placeholder);
    RUN(binding_no_dot_returns_placeholder);
    RUN(binding_bad_key_returns_placeholder);
    RUN(binding_in_template);

    printf("\nlist_filter_items tests:\n");
    RUN(filter_empty_passes_all);
    RUN(filter_null_passes_all);
    RUN(filter_star_passes_all);
    RUN(filter_exact_id_match);
    RUN(filter_glob_id_prefix);
    RUN(filter_glob_label_substring);
    RUN(filter_index_single);
    RUN(filter_index_range);
    RUN(filter_exclusion_only);
    RUN(filter_mixed_include_exclude);
    RUN(filter_case_insensitive);
    RUN(filter_no_match_returns_zero);
    RUN(filter_multi_index_rules);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
