// ============================================================================
// Unit tests: component_registry
// ============================================================================
// Tests: add, find, iterate, overflow, duplicate detection, category filtering.
// Build: CMake/CTest (see tests/CMakeLists.txt)

#include "../src/app/component_registry.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>

static std::vector<uint8_t> s_saved_body;
static int s_free_count = 0;

static bool save_body_ok(const uint8_t* data, size_t len) {
    s_saved_body.assign(data, data + len);
    return true;
}

static bool save_body_fail(const uint8_t*, size_t) {
    return false;
}

static void* alloc_fail(size_t) {
    return nullptr;
}

static void counted_free(void* ptr) {
    s_free_count++;
    free(ptr);
}

// ---- test_add_and_find ----
static void test_add_and_find() {
    printf("  test_add_and_find...");
    component_registry_reset();

    static ComponentDef comp = {
        .id = "wifi",
        .category = "device",
        .display_name = "WiFi Settings",
        .nav_order = 10,
        .get_config = nullptr,
        .save_config = nullptr,
        .save_config_body = nullptr,
        .delete_config = nullptr,
        .custom_actions = nullptr,
        .num_custom_actions = 0,
           .fragment_id = "wifi",
           .portal_script = nullptr,
           .portal_style = nullptr
    };

    bool added = component_registry_add(&comp);
    assert(added);
    assert(component_registry_count() == 1);

    ComponentDef* found = component_registry_find("wifi");
    assert(found != nullptr);
    assert(strcmp(found->id, "wifi") == 0);
    assert(strcmp(found->display_name, "WiFi Settings") == 0);
    assert(found->nav_order == 10);

    ComponentDef* not_found = component_registry_find("bluetooth");
    assert(not_found == nullptr);

    printf(" PASS\n");
}

// ---- test_max_components ----
static void test_max_components() {
    printf("  test_max_components...");
    component_registry_reset();

    static ComponentDef comps[MAX_PORTAL_COMPONENTS + 1];
    static char ids[MAX_PORTAL_COMPONENTS + 1][16];

    for (int i = 0; i <= MAX_PORTAL_COMPONENTS; i++) {
        snprintf(ids[i], sizeof(ids[i]), "comp-%d", i);
        comps[i] = {
            .id = ids[i],
            .category = "test",
            .display_name = "Test",
            .nav_order = i,
            .get_config = nullptr,
            .save_config = nullptr,
            .save_config_body = nullptr,
            .delete_config = nullptr,
            .custom_actions = nullptr,
            .num_custom_actions = 0,
            .fragment_id = nullptr,
            .portal_script = nullptr,
            .portal_style = nullptr
        };
    }

    // Fill to capacity
    for (int i = 0; i < MAX_PORTAL_COMPONENTS; i++) {
        bool ok = component_registry_add(&comps[i]);
        assert(ok);
    }
    assert(component_registry_count() == MAX_PORTAL_COMPONENTS);

    // Overflow must fail
    bool overflow = component_registry_add(&comps[MAX_PORTAL_COMPONENTS]);
    assert(!overflow);
    assert(component_registry_count() == MAX_PORTAL_COMPONENTS);

    printf(" PASS\n");
}

// ---- test_duplicate_id ----
static void test_duplicate_id() {
    printf("  test_duplicate_id...");
    component_registry_reset();

    static ComponentDef comp1 = {
        .id = "timers",
        .category = "actions",
        .display_name = "Timers",
        .nav_order = 30,
        .get_config = nullptr, .save_config = nullptr,
        .save_config_body = nullptr, .delete_config = nullptr,
        .custom_actions = nullptr, .num_custom_actions = 0,
            .fragment_id = "timers",
            .portal_script = nullptr,
            .portal_style = nullptr
    };
    static ComponentDef comp2 = {
        .id = "timers",
        .category = "actions",
        .display_name = "Timers Duplicate",
        .nav_order = 31,
        .get_config = nullptr, .save_config = nullptr,
        .save_config_body = nullptr, .delete_config = nullptr,
        .custom_actions = nullptr, .num_custom_actions = 0,
        .fragment_id = "timers",
        .portal_script = nullptr,
        .portal_style = nullptr
    };

    bool first = component_registry_add(&comp1);
    assert(first);

    bool second = component_registry_add(&comp2);
    assert(!second);  // duplicate rejected

    assert(component_registry_count() == 1);
    assert(strcmp(component_registry_find("timers")->display_name, "Timers") == 0);

    printf(" PASS\n");
}

// ---- test_category_iteration ----
static void test_category_iteration() {
    printf("  test_category_iteration...");
    component_registry_reset();

    static ComponentDef device_comp = {
        .id = "wifi", .category = "device", .display_name = "WiFi",
        .nav_order = 10,
        .get_config = nullptr, .save_config = nullptr,
        .save_config_body = nullptr, .delete_config = nullptr,
        .custom_actions = nullptr, .num_custom_actions = 0,
           .fragment_id = "wifi",
           .portal_script = nullptr,
           .portal_style = nullptr
    };
    static ComponentDef actions_comp1 = {
        .id = "swipe-actions", .category = "actions", .display_name = "Swipe Actions",
        .nav_order = 10,
        .get_config = nullptr, .save_config = nullptr,
        .save_config_body = nullptr, .delete_config = nullptr,
        .custom_actions = nullptr, .num_custom_actions = 0,
           .fragment_id = "swipe-actions",
           .portal_script = nullptr,
           .portal_style = nullptr
    };
    static ComponentDef actions_comp2 = {
        .id = "boot-actions", .category = "actions", .display_name = "Boot Actions",
        .nav_order = 20,
        .get_config = nullptr, .save_config = nullptr,
        .save_config_body = nullptr, .delete_config = nullptr,
        .custom_actions = nullptr, .num_custom_actions = 0,
           .fragment_id = "boot-actions",
           .portal_script = nullptr,
           .portal_style = nullptr
    };

    component_registry_add(&device_comp);
    component_registry_add(&actions_comp1);
    component_registry_add(&actions_comp2);

    // Iterate "actions" category — should yield exactly 2
    std::vector<const char*> action_ids;
    component_registry_for_category("actions",
        [](ComponentDef* def, void* ctx) {
            auto* v = static_cast<std::vector<const char*>*>(ctx);
            v->push_back(def->id);
        }, &action_ids);

    assert(action_ids.size() == 2);
    assert(strcmp(action_ids[0], "swipe-actions") == 0);
    assert(strcmp(action_ids[1], "boot-actions") == 0);

    // Iterate "device" — should yield exactly 1
    std::vector<const char*> device_ids;
    component_registry_for_category("device",
        [](ComponentDef* def, void* ctx) {
            auto* v = static_cast<std::vector<const char*>*>(ctx);
            v->push_back(def->id);
        }, &device_ids);

    assert(device_ids.size() == 1);

    // Iterate "sensors" — should yield 0
    std::vector<const char*> sensor_ids;
    component_registry_for_category("sensors",
        [](ComponentDef* def, void* ctx) {
            auto* v = static_cast<std::vector<const char*>*>(ctx);
            v->push_back(def->id);
        }, &sensor_ids);

    assert(sensor_ids.empty());

    printf(" PASS\n");
}

// ---- test_nav_order ----
static void test_nav_order() {
    printf("  test_nav_order...");
    component_registry_reset();

    // Register in reverse nav_order to verify sorting at query time
    static ComponentDef comp_c = {
        .id = "timers", .category = "actions", .display_name = "Timers",
        .nav_order = 30,
        .get_config = nullptr, .save_config = nullptr,
        .save_config_body = nullptr, .delete_config = nullptr,
        .custom_actions = nullptr, .num_custom_actions = 0,
           .fragment_id = "timers",
           .portal_script = nullptr,
           .portal_style = nullptr
    };
    static ComponentDef comp_a = {
        .id = "swipe-actions", .category = "actions", .display_name = "Swipe Actions",
        .nav_order = 10,
        .get_config = nullptr, .save_config = nullptr,
        .save_config_body = nullptr, .delete_config = nullptr,
        .custom_actions = nullptr, .num_custom_actions = 0,
           .fragment_id = "swipe-actions",
           .portal_script = nullptr,
           .portal_style = nullptr
    };
    static ComponentDef comp_b = {
        .id = "boot-actions", .category = "actions", .display_name = "Boot Actions",
        .nav_order = 20,
        .get_config = nullptr, .save_config = nullptr,
        .save_config_body = nullptr, .delete_config = nullptr,
        .custom_actions = nullptr, .num_custom_actions = 0,
           .fragment_id = "boot-actions",
           .portal_script = nullptr,
           .portal_style = nullptr
    };

    component_registry_add(&comp_c);  // nav_order=30
    component_registry_add(&comp_a);  // nav_order=10
    component_registry_add(&comp_b);  // nav_order=20

    // Collect all "actions" and sort by nav_order (as the nav handler would)
    struct Entry { const char* id; int order; };
    std::vector<Entry> entries;
    component_registry_for_category("actions",
        [](ComponentDef* def, void* ctx) {
            auto* v = static_cast<std::vector<Entry>*>(ctx);
            v->push_back({def->id, def->nav_order});
        }, &entries);

    assert(entries.size() == 3);

    std::sort(entries.begin(), entries.end(),
        [](const Entry& a, const Entry& b) { return a.order < b.order; });

    assert(strcmp(entries[0].id, "swipe-actions") == 0);
    assert(entries[0].order == 10);
    assert(strcmp(entries[1].id, "boot-actions") == 0);
    assert(entries[1].order == 20);
    assert(strcmp(entries[2].id, "timers") == 0);
    assert(entries[2].order == 30);

    printf(" PASS\n");
}

// ---- test_get_by_index ----
static void test_get_by_index() {
    printf("  test_get_by_index...");
    component_registry_reset();

    static ComponentDef comp = {
        .id = "ble", .category = "connectivity", .display_name = "BLE",
        .nav_order = 20,
        .get_config = nullptr, .save_config = nullptr,
        .save_config_body = nullptr, .delete_config = nullptr,
        .custom_actions = nullptr, .num_custom_actions = 0,
           .fragment_id = "ble",
           .portal_script = nullptr,
           .portal_style = nullptr
    };

    component_registry_add(&comp);

    assert(component_registry_get(0) == &comp);
    assert(component_registry_get(1) == nullptr);  // out of bounds
    assert(component_registry_get(255) == nullptr); // way out of bounds

    printf(" PASS\n");
}

// ---- test_custom_actions ----
static void test_custom_actions() {
    printf("  test_custom_actions...");
    component_registry_reset();

    static bool handler_called = false;
    static ComponentAction actions[] = {
        {"sleep", HTTP_GET, [](AsyncWebServerRequest*) { handler_called = true; }, nullptr},
        {"sleep", HTTP_POST, [](AsyncWebServerRequest*) {}, nullptr},
        {"wake",  HTTP_POST, [](AsyncWebServerRequest*) {}, nullptr},
    };

    static ComponentDef comp = {
        .id = "display", .category = "display", .display_name = "Display",
        .nav_order = 10,
        .get_config = nullptr, .save_config = nullptr,
        .save_config_body = nullptr, .delete_config = nullptr,
        .custom_actions = actions,
        .num_custom_actions = 3,
           .fragment_id = "brightness",
           .portal_script = nullptr,
           .portal_style = nullptr
    };

    component_registry_add(&comp);

    ComponentDef* found = component_registry_find("display");
    assert(found != nullptr);
    assert(found->num_custom_actions == 3);

    // Find action by (name, method) pair
    bool found_get_sleep = false;
    bool found_post_sleep = false;
    for (uint8_t i = 0; i < found->num_custom_actions; i++) {
        if (strcmp(found->custom_actions[i].name, "sleep") == 0) {
            if (found->custom_actions[i].method == HTTP_GET) found_get_sleep = true;
            if (found->custom_actions[i].method == HTTP_POST) found_post_sleep = true;
        }
    }
    assert(found_get_sleep);
    assert(found_post_sleep);

    // Verify handler callable
    handler_called = false;
    found->custom_actions[0].handler(nullptr);
    assert(handler_called);

    printf(" PASS\n");
}

static void test_save_body_single_chunk() {
    printf("  test_save_body_single_chunk...");
    AsyncWebServerRequest request;
    uint8_t body[] = {'{', '}', '\n'};
    s_saved_body.clear();
    component_handle_save_body(&request, body, sizeof(body), 0, sizeof(body),
                               save_body_ok);
    assert(request.response_code == 200);
    assert(s_saved_body == std::vector<uint8_t>(body, body + sizeof(body)));
    assert(request._tempObject == nullptr);
    printf(" PASS\n");
}

static void test_save_body_multiple_chunks() {
    printf("  test_save_body_multiple_chunks...");
    AsyncWebServerRequest request;
    uint8_t first[] = {'a', 'b'};
    uint8_t second[] = {'c', 'd', 'e'};
    s_saved_body.clear();
    component_handle_save_body(&request, first, sizeof(first), 0, 5, save_body_ok);
    assert(request.response_code == 0);
    assert(request._tempObject != nullptr);
    component_handle_save_body(&request, second, sizeof(second), 2, 5, save_body_ok);
    assert(request.response_code == 200);
    assert(s_saved_body == std::vector<uint8_t>({'a', 'b', 'c', 'd', 'e'}));
    assert(request._tempObject == nullptr);
    printf(" PASS\n");
}

static void test_save_body_rejects_overlap_or_gap() {
    printf("  test_save_body_rejects_overlap_or_gap...");
    uint8_t first[] = {'a', 'b'};
    uint8_t second[] = {'c', 'd'};

    AsyncWebServerRequest overlap_request;
    component_handle_save_body(&overlap_request, first, sizeof(first), 0, 4,
                               save_body_ok);
    component_handle_save_body(&overlap_request, second, sizeof(second), 1, 4,
                               save_body_ok);
    assert(overlap_request.response_code == 400);
    assert(overlap_request._tempObject == nullptr);

    AsyncWebServerRequest gap_request;
    component_handle_save_body(&gap_request, first, sizeof(first), 0, 5,
                               save_body_ok);
    component_handle_save_body(&gap_request, second, sizeof(second), 3, 5,
                               save_body_ok);
    assert(gap_request.response_code == 400);
    assert(gap_request._tempObject == nullptr);
    printf(" PASS\n");
}

static void test_save_body_over_limit() {
    printf("  test_save_body_over_limit...");
    AsyncWebServerRequest request;
    uint8_t byte = 0;
    component_handle_save_body(&request, &byte, 1, 0, 4097, save_body_ok);
    assert(request.response_code == 413);
    assert(request._tempObject == nullptr);
    printf(" PASS\n");
}

static void test_save_body_allocation_failure() {
    printf("  test_save_body_allocation_failure...");
    AsyncWebServerRequest request;
    uint8_t byte = 0;
    component_registry_set_body_allocator_for_test(alloc_fail, nullptr);
    component_handle_save_body(&request, &byte, 1, 0, 1, save_body_ok);
    component_registry_set_body_allocator_for_test(nullptr, nullptr);
    assert(request.response_code == 500);
    assert(request._tempObject == nullptr);
    printf(" PASS\n");
}

static void test_save_body_saver_failure_frees() {
    printf("  test_save_body_saver_failure_frees...");
    AsyncWebServerRequest request;
    uint8_t byte = 0;
    s_free_count = 0;
    component_registry_set_body_allocator_for_test(malloc, counted_free);
    component_handle_save_body(&request, &byte, 1, 0, 1, save_body_fail);
    component_registry_set_body_allocator_for_test(nullptr, nullptr);
    assert(request.response_code == 500);
    assert(request._tempObject == nullptr);
    assert(s_free_count == 1);
    printf(" PASS\n");
}

// ---- main ----
int main() {
    printf("=== component_registry tests ===\n");

    test_add_and_find();
    test_max_components();
    test_duplicate_id();
    test_category_iteration();
    test_nav_order();
    test_get_by_index();
    test_custom_actions();
    test_save_body_single_chunk();
    test_save_body_multiple_chunks();
    test_save_body_rejects_overlap_or_gap();
    test_save_body_over_limit();
    test_save_body_allocation_failure();
    test_save_body_saver_failure_frees();

    printf("=== All component_registry tests passed ===\n");
    return 0;
}
