#include <cassert>
#include <cstring>
#include <string>
#include <lvgl.h>
#include <new>
#include <cstdio>
#include <utility>
#include <algorithm>

#include "log_manager.h"

void log_write(LogLevel, const char*, const char*, ...) {}
bool log_diagnostics_enabled(const char*) { return false; }

#define DISPLAY_MANAGER_H
#define TOUCH_MANAGER_H
#define HAS_TOUCH 1
static bool fail_allocation = false;
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    if (fail_allocation) { fail_allocation = false; return nullptr; }
    return ::operator new(size);
}
static uint32_t now_ms = 0;
extern "C" unsigned long millis() { return now_ms; }
static bool ota = false;
bool ota_activity_is_active() { return ota; }
static int touch = 1;
int* touchManager = &touch;
uint8_t touch_manager_contact_capacity() { return touchManager ? 1 : 0; }
void touch_manager_cancel_physical_input() {}
class DisplayManager {
public:
    int getActiveWidth() { return 466; }
    int getActiveHeight() { return 466; }
    void lock() {}
    void unlock() {}
};
static DisplayManager manager;
DisplayManager* displayManager = &manager;
void display_manager_lock_if_needed(bool* locked) { *locked = false; }
void display_manager_unlock_if_needed(bool) {}

#include "../src/app/screens/splash_screen.cpp"
class AsyncWebServer;
#include "../src/app/screens/wifi_setup_screen.cpp"
#include "../src/app/device_dialog.cpp"
#include "../src/app/dialog_appearance.cpp"
#include "../src/app/components/dialog_appearance_component.cpp"

static LoopBridgeExec offline_job = nullptr;
static WifiSetupScreen* offline_context = nullptr;
static bool offline_allowed = false;
static int offline_requests = 0;
LoopBridgeResult loop_bridge_enqueue(LoopBridgeExec exec, const void* context, size_t size) {
    if (offline_job) return LOOP_BRIDGE_BUSY;
    assert(size == sizeof(offline_context));
    memcpy(&offline_context, context, size);
    offline_job = exec;
    ++offline_requests;
    return LOOP_BRIDGE_OK;
}
bool app_continue_offline_from_setup() { return offline_allowed; }
bool web_portal_ap_get_setup_info(char* ssid, size_t ssid_size,
        char* password, size_t password_size, char* ip, size_t ip_size) {
    snprintf(ssid, ssid_size, "Device-setup");
    snprintf(password, password_size, "test-password");
    snprintf(ip, ip_size, "192.168.4.1");
    return true;
}
static void run_offline_job() {
    bool ok = false;
    char message[128];
    assert(offline_job);
    offline_job(&offline_context, &ok, message, sizeof(message));
    assert(ok == offline_allowed);
    offline_job = nullptr;
}

static lv_obj_t* find_label(lv_obj_t* parent, const char* text) {
    if (lv_obj_check_type(parent, &lv_label_class) &&
            strstr(lv_label_get_text(parent), text)) return parent;
    for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
        if (auto* found = find_label(lv_obj_get_child(parent, i), text)) return found;
    }
    return nullptr;
}

static void wifi_setup_checks(lv_display_t* display) {
    for (const auto& dimensions : {std::pair<int, bool>{720, false}, {720, true},
                                  {466, false}, {466, true}}) {
        const int size = dimensions.first;
        const bool light = dimensions.second;
        lv_display_set_resolution(display, size, size);
        DeviceConfig config{};
        SplashScreen splash;
        splash.setAppearance(18, light ? 0 : 0xFFFFFF, light ? 0xFFFFFF : 0, true);
        splash.create();
        constexpr int logo_width = 320;
        constexpr int logo_height = 177;
        auto* pixels = static_cast<uint16_t*>(malloc(logo_width * logo_height * sizeof(uint16_t)));
        assert(pixels);
        std::fill(pixels, pixels + logo_width * logo_height, 0xABCD);
        assert(splash.setCustomLogo(pixels, logo_width, logo_height));
        WifiSetupScreen setup(&config);
        setup.setAppearance(18, light ? 0 : 0xFFFFFF, light ? 0xFFFFFF : 0);
        setup.create();
        setup.takeLogo(splash);
        splash.hide();
        splash.destroy();
        setup.show();
        setup.update();
        auto* screen = lv_screen_active();
        lv_obj_update_layout(screen);
        auto* content = lv_obj_get_child(screen, 0);
        assert(abs(lv_obj_get_y(content) * 2 + lv_obj_get_height(content) - size) <= 1);
        assert(lv_obj_get_scroll_bottom(screen) <= 0);
        auto* logo = lv_obj_get_child(content, -1);
        assert(!lv_obj_has_flag(logo, LV_OBJ_FLAG_HIDDEN));
        auto* image = lv_obj_get_child(logo, 0);
        assert(lv_obj_get_width(image) <= lv_obj_get_width(logo));
        assert(lv_obj_get_height(image) <= lv_obj_get_height(logo));
        assert(lv_obj_get_scroll_bottom(logo) == 0);
        const auto* source = static_cast<const lv_image_dsc_t*>(lv_image_get_src(image));
        assert(source->data == reinterpret_cast<const uint8_t*>(pixels));
        assert(pixels[0] == 0xABCD); // The AP screen still owns the decoded image.
        assert(find_label(screen, "1. "));
        assert(find_label(screen, "2. Browse to http://192.168.4.1/"));
        assert(find_label(screen, "3. "));
        auto* label = find_label(screen, "Stop AP and\ncontinue offline");
        assert(label);
        auto* button = lv_obj_get_parent(label);
        assert(lv_obj_get_height(label) >= lv_font_get_line_height(&lv_font_montserrat_18) * 2);
        assert(lv_obj_get_height(button) > lv_obj_get_height(label));
        lv_area_t original_instructions;
        lv_area_t original_button;
        lv_obj_get_coords(find_label(screen, "1. "), &original_instructions);
        assert(original_instructions.y1 < size / 2);
        lv_obj_get_coords(button, &original_button);
        lv_obj_add_state(button, LV_STATE_PRESSED);
        assert((lv_color_to_u32(lv_obj_get_style_bg_color(button, LV_PART_MAIN)) & 0xFFFFFFU) ==
            (light ? 0xFFFFFFU : 0U));
        lv_obj_clear_state(button, LV_STATE_PRESSED);

        offline_allowed = false;
        const int requests = offline_requests;
        lv_obj_send_event(button, LV_EVENT_CLICKED, nullptr);
        assert(find_label(screen, "Starting offline"));
        assert(lv_obj_has_state(button, LV_STATE_DISABLED));
        lv_obj_update_layout(screen);
        lv_area_t tapped_instructions;
        lv_area_t tapped_button;
        lv_obj_get_coords(find_label(screen, "1. "), &tapped_instructions);
        lv_obj_get_coords(button, &tapped_button);
        assert(original_instructions.y1 == tapped_instructions.y1);
        assert(original_instructions.y2 == tapped_instructions.y2);
        assert(original_button.y1 == tapped_button.y1);
        assert(original_button.y2 == tapped_button.y2);
        lv_obj_send_event(button, LV_EVENT_CLICKED, nullptr);
        assert(offline_requests == requests + 1);
        run_offline_job();
        setup.update();
        assert(!lv_obj_has_state(button, LV_STATE_DISABLED));
        assert(find_label(screen, "Stop AP and\ncontinue offline"));
        assert(find_label(screen, "Finish any update or save"));

        offline_job = [](const void*, bool*, char*, size_t) {};
        lv_obj_send_event(button, LV_EVENT_CLICKED, nullptr);
        assert(!lv_obj_has_state(button, LV_STATE_DISABLED));
        assert(find_label(screen, "Device busy"));
        offline_job = nullptr;
        offline_allowed = true;
        lv_obj_send_event(button, LV_EVENT_CLICKED, nullptr);
        run_offline_job();
        assert(lv_obj_has_state(button, LV_STATE_DISABLED));
        setup.hide();
        setup.destroy();
    }
    lv_display_set_resolution(display, 466, 466);
    {
        SplashScreen splash;
        splash.create();
        WifiSetupScreen setup(nullptr);
        setup.setAppearance(36, 0, 0xFFFFFF);
        setup.create();
        setup.takeLogo(splash);
        splash.hide();
        setup.show();
        setup.update();
        lv_obj_update_layout(lv_screen_active());
        auto* content = lv_obj_get_child(lv_screen_active(), 0);
        auto* logo = lv_obj_get_child(content, -1);
        assert(lv_obj_has_flag(logo, LV_OBJ_FLAG_HIDDEN));
        assert(lv_obj_get_y(content) >= 0);
        setup.hide();
    }
}

static ConfirmPayload confirmation() {
    ConfirmPayload content{};
    strcpy(content.message, "Continue?\nThis cannot be undone.");
    strcpy(content.approve_text, "Continue");
    strcpy(content.cancel_text, "Cancel");
    content.timeout_ms = 10000;
    return content;
}

static uint32_t reserve(ActionContinuationOwner owner = ACTION_CONTINUATION_OWNER_LOOP) {
    ButtonAction suffix{};
    strcpy(suffix.type, ACTION_TYPE_SYSTEM);
    uint32_t token = 0;
    assert(action_continuation_begin(&suffix, 1, "Dialog test", &token, owner));
    assert(action_continuation_set_timeout(token, 0));
    action_continuation_mark_pending(token);
    return token;
}

static lv_obj_t* dialog_box() { return lv_obj_get_child(lv_obj_get_child(lv_layer_top(), -1), 0); }
static lv_obj_t* cancel_button() { return lv_obj_get_child(dialog_box(), 1); }
static lv_obj_t* approve_button() { return lv_obj_get_child(dialog_box(), 2); }
static void assert_dialog_selection(bool cancel, bool approve) {
    assert((lv_obj_get_style_outline_width(cancel_button(), LV_PART_MAIN) > 0) == cancel);
    assert((lv_obj_get_style_outline_width(approve_button(), LV_PART_MAIN) > 0) == approve);
}
static uint32_t pixels[720 * 720];

static void dialog_lifecycle_checks() {
    auto content = confirmation();
    uint32_t token = reserve(ACTION_CONTINUATION_OWNER_LVGL);
    assert(device_dialog_confirm(content, token));
    assert_dialog_selection(false, false);
    lv_obj_send_event(approve_button(), LV_EVENT_CLICKED, nullptr);
    assert(!device_dialog_active());
    assert(action_continuation_take(nullptr, nullptr, nullptr, 0) == ACTION_CONTINUATION_NONE);
    ButtonAction suffix[MAX_BUTTON_ACTIONS - 1]{};
    uint8_t count = 0;
    assert(action_continuation_take(suffix, &count, nullptr, 0, ACTION_CONTINUATION_OWNER_LVGL) == ACTION_CONTINUATION_SUCCESS);
    assert(count == 1 && !strcmp(suffix[0].type, ACTION_TYPE_SYSTEM));
    assert(!action_continuation_complete(token, true));

    for (const char* first : {"tap", "next", "previous"}) {
        token = reserve();
        assert(device_dialog_confirm(content, token));
        assert_dialog_selection(false, false);
        device_dialog_navigate("tap_hold");
        assert_dialog_selection(false, false);
        device_dialog_navigate(first);
        const bool approve = !strcmp(first, "previous");
        assert(device_dialog_active());
        assert_dialog_selection(!approve, approve);
        assert(action_continuation_take(nullptr, nullptr, nullptr, 0) == ACTION_CONTINUATION_NONE);
        if (!strcmp(first, "next")) {
            device_dialog_navigate("next");
            assert_dialog_selection(false, true);
            device_dialog_navigate("previous");
            assert_dialog_selection(true, false);
        }
        device_dialog_navigate("tap");
        assert(!device_dialog_active());
        assert(action_continuation_take(nullptr, nullptr, nullptr, 0) ==
            (approve ? ACTION_CONTINUATION_SUCCESS : ACTION_CONTINUATION_NONE));
        assert(!action_continuation_complete(token, true));
        assert(action_continuation_take(nullptr, nullptr, nullptr, 0) == ACTION_CONTINUATION_NONE);
    }

    token = reserve();
    assert(device_dialog_confirm(content, token));
    lv_obj_send_event(cancel_button(), LV_EVENT_CLICKED, nullptr);
    assert(!device_dialog_active() && !action_continuation_complete(token, true));

    for (uint32_t timeout : {61000U, 300000U, 0U}) {
        now_ms = 0;
        content.timeout_ms = timeout;
        token = reserve();
        assert(device_dialog_confirm(content, token));
        now_ms = timeout ? timeout - 1 : 900000;
        device_dialog_loop();
        assert(device_dialog_active());
        assert(action_continuation_take(nullptr, nullptr, nullptr, 0) == ACTION_CONTINUATION_NONE);
        if (timeout) {
            ++now_ms;
            device_dialog_loop();
            assert(!device_dialog_active());
        } else {
            device_dialog_close();
        }
        assert(!action_continuation_complete(token, true));
    }
    content = confirmation();
    for (bool old_confirm : {false, true}) {
        for (bool new_confirm : {false, true}) {
            uint32_t old_token = old_confirm ? reserve() : 0;
            assert(old_confirm ? device_dialog_confirm(content, old_token) : device_dialog_notify("Old", 1000));
            const uint32_t epoch = device_dialog_input_generation();
            if (old_confirm) device_dialog_navigate("previous");
            uint32_t new_token = new_confirm ? reserve() : 0;
            assert(new_confirm ? device_dialog_confirm(content, new_token) : device_dialog_notify("New", 300000));
            assert(device_dialog_input_generation() != epoch);
            assert(!old_confirm || !action_continuation_complete(old_token, true));
            if (new_confirm) {
                assert_dialog_selection(false, false);
                device_dialog_navigate("tap");
                assert(device_dialog_active());
                assert_dialog_selection(true, false);
                device_dialog_navigate("tap");
                assert(!action_continuation_complete(new_token, true));
            } else {
                now_ms += 1001;
                device_dialog_loop(); // old notification deadline must not close replacement
                assert(device_dialog_active());
                device_dialog_navigate("tap");
            }
            assert(!device_dialog_active());
        }
    }

    // Failed resource/layout replacements preserve both the object and waiting token.
    token = reserve();
    assert(device_dialog_confirm(content, token));
    device_dialog_navigate("previous");
    assert_dialog_selection(false, true);
    lv_obj_t* original = dialog_box();
    fail_allocation = true;
    assert(!device_dialog_notify("Allocation failure", 1000));
    assert(dialog_box() == original);
    ota = true;
    assert(!device_dialog_notify("OTA owns the display", 1000));
    assert(dialog_box() == original);
    ota = false;
    DialogTemplates invalid;
    invalid.confirmation.padding = 48;
    invalid.confirmation.width = 20;
    device_dialog_set_templates(invalid);
    uint32_t failed_token = reserve();
    assert(!device_dialog_confirm(content, failed_token));
    action_continuation_release(failed_token);
    assert(dialog_box() == original);
    device_dialog_set_templates(DialogTemplates{});
    assert_dialog_selection(false, true);
    device_dialog_navigate("tap");
    assert(action_continuation_take(nullptr, nullptr, nullptr, 0) == ACTION_CONTINUATION_SUCCESS);

    token = reserve();
    assert(device_dialog_confirm(content, token));
    auto* descriptor = lv_obj_get_event_dsc(approve_button(), 0);
    auto old_callback = lv_event_dsc_get_cb(descriptor);
    void* old_user_data = lv_event_dsc_get_user_data(descriptor);
    assert(device_dialog_notify("Replacement", 0));
    auto* overlay = lv_obj_get_child(lv_layer_top(), -1);
    lv_obj_add_event_cb(overlay, old_callback, LV_EVENT_VALUE_CHANGED, old_user_data);
    lv_obj_send_event(overlay, LV_EVENT_VALUE_CHANGED, nullptr);
    assert(device_dialog_active() && !action_continuation_complete(token, true));
    now_ms += 900000;
    device_dialog_loop();
    assert(device_dialog_active()); // persistent notification
    device_dialog_close();

    assert(device_dialog_notify("300 seconds", 300000));
    now_ms += 299999;
    device_dialog_loop();
    assert(device_dialog_active());
    ++now_ms;
    device_dialog_loop();
    assert(!device_dialog_active());

    token = reserve();
    content.timeout_ms = 1000;
    assert(device_dialog_confirm(content, token));
    device_dialog_navigate("previous");
    now_ms += 1000;
    device_dialog_navigate("tap");
    assert(!device_dialog_active() && !action_continuation_complete(token, true));
    content.timeout_ms = 0;

    touchManager = nullptr;
    token = reserve();
    assert(!device_dialog_confirm(content, token));
    action_continuation_release(token);
    touchManager = &touch;

    DialogTemplates zero;
    zero.notification.background_opacity = zero.notification.backdrop_opacity = 0;
    device_dialog_set_templates(zero);
    assert(device_dialog_notify("Transparent but modal", 0));
    overlay = lv_obj_get_child(lv_layer_top(), -1);
    assert(lv_obj_has_flag(overlay, LV_OBJ_FLAG_CLICKABLE));
    assert(lv_obj_get_style_bg_opa(overlay, LV_PART_MAIN) == 0);
    assert(lv_obj_get_style_bg_opa(dialog_box(), LV_PART_MAIN) == 0);
    zero.notification.background_opacity = 100;
    device_dialog_set_templates(zero);
    assert(lv_obj_get_style_bg_opa(dialog_box(), LV_PART_MAIN) == 0); // snapshot, not live styling
    device_dialog_close();

    JsonDocument doc;
    dialog_appearance_write(doc.to<JsonObject>(), zero);
    DialogTemplates restored;
    assert(!dialog_appearance_read(doc.as<JsonObjectConst>(), restored));
    assert(restored.notification.background_opacity == 100 && restored.notification.backdrop_opacity == 0);
    doc["confirmation"]["padding"] = 49;
    const uint8_t saved_padding = restored.confirmation.padding;
    assert(dialog_appearance_read(doc.as<JsonObjectConst>(), restored));
    assert(restored.confirmation.padding == saved_padding);
    doc.clear();
    doc["notification"]["background_opacity"] = 0;
    assert(!dialog_appearance_read(doc.as<JsonObjectConst>(), restored));
    assert(restored.notification.background_opacity == 0 && restored.confirmation.padding == 16);
    device_dialog_set_templates(DialogTemplates{});

    DialogAppearance test_style;
    test_style.background_opacity = 0;
    assert(device_dialog_test(false, test_style));
    assert(lv_obj_get_style_bg_opa(dialog_box(), LV_PART_MAIN) == 0);
    auto* test_original = dialog_box();
    touchManager = nullptr;
    assert(!device_dialog_test(true, test_style));
    assert(dialog_box() == test_original);
    touchManager = &touch;
    fail_allocation = true;
    assert(!device_dialog_test(true, test_style));
    assert(dialog_box() == test_original);
    assert(device_dialog_test(true, test_style));
    assert(lv_obj_get_style_bg_opa(dialog_box(), LV_PART_MAIN) == 0);
    device_dialog_navigate("previous");
    device_dialog_navigate("tap");
    uint8_t test_count = 99;
    assert(action_continuation_take(nullptr, &test_count, nullptr, 0) == ACTION_CONTINUATION_SUCCESS);
    assert(test_count == 0);
    assert(device_dialog_notify("Saved appearance unchanged", 1000));
    assert(lv_obj_get_style_bg_opa(dialog_box(), LV_PART_MAIN) == LV_OPA_COVER);
    device_dialog_close();
    assert(device_dialog_test(true, test_style));
    now_ms += 10000;
    device_dialog_loop();
    assert(!device_dialog_active());
}

static void assert_inside_canvas(lv_obj_t* object) {
    lv_area_t area;
    lv_obj_get_coords(object, &area);
    assert(area.x1 >= 0 && area.y1 >= 0 && area.x2 < 466 && area.y2 < 466);
}

static void dialog_test_endpoint_checks() {
    for (const char* type : {"notification", "confirmation"}) {
        JsonDocument doc;
        doc["type"] = type;
        DialogTemplates appearance;
        JsonDocument templates;
        dialog_appearance_write(templates.to<JsonObject>(), appearance);
        doc["appearance"] = templates[type];
        doc["appearance"]["background_opacity"] = 0;
        doc["appearance"]["background_pattern"] = "diagonal";
        doc["appearance"]["background_pattern_color"] = "#226D78";
        doc["appearance"]["pattern_spacing"] = 12;
        doc["appearance"]["pattern_stroke_width"] = 2;
        std::string body;
        serializeJson(doc, body);
        AsyncWebServerRequest request;
        const size_t split = body.size() / 2;
        auto* bytes = reinterpret_cast<uint8_t*>(body.data());
        dialog_appearance_test_post(&request, bytes, split, 0, body.size());
        assert(!request.response_code);
        dialog_appearance_test_post(&request, bytes + split, body.size() - split, split, body.size());
        assert(request.response_code == 200 && device_dialog_active());
        assert(lv_obj_get_style_bg_opa(dialog_box(), LV_PART_MAIN) == 0);
        assert(!request._tempObject);
        device_dialog_close();
    }
    std::string body = "{\"type\":\"notification\",\"appearance\":{\"padding\":49}}";
    AsyncWebServerRequest invalid;
    dialog_appearance_test_post(&invalid, reinterpret_cast<uint8_t*>(body.data()), body.size(), 0, body.size());
    assert(invalid.response_code == 500 && !device_dialog_active());
    assert(invalid.response_body.find("Dialog test failed") != std::string::npos);
    assert(invalid.response_body.find("save failed") == std::string::npos);
}

static void dialog_pattern_checks(lv_display_t* display) {
    DialogTemplates appearance;
    appearance.notification.background_pattern = FillPattern::Diagonal;
    appearance.notification.background_color = 0xFF0000;
    appearance.notification.background_pattern_color = 0x00FF00;
    appearance.notification.pattern_spacing = 12;
    appearance.notification.pattern_stroke_width = 2;
    appearance.notification.backdrop_opacity = 0;
    appearance.notification.corner_radius = appearance.notification.border_width = 0;
    appearance.notification.width = appearance.notification.height = 50;
    appearance.confirmation = appearance.notification;
    JsonDocument doc;
    dialog_appearance_write(doc.to<JsonObject>(), appearance);
    std::string stored;
    serializeJson(doc, stored);
    doc.clear();
    assert(!deserializeJson(doc, stored));
    DialogTemplates restored;
    assert(!dialog_appearance_read(doc.as<JsonObjectConst>(), restored));
    assert(restored.notification.background_pattern == FillPattern::Diagonal);
    assert(restored.confirmation.background_pattern_color == 0x00FF00);
    assert(restored.notification.pattern_spacing == 12 && restored.confirmation.pattern_stroke_width == 2);
    for (const char* pattern : {"none", "diagonal", "reverse_diagonal", "crosshatch", "dots", "checkerboard"}) {
        doc["notification"]["background_pattern"] = pattern;
        assert(!dialog_appearance_read(doc.as<JsonObjectConst>(), restored));
        assert(!strcmp(fill_pattern_name(restored.notification.background_pattern), pattern));
    }
    for (const auto& geometry : {std::pair<unsigned, unsigned>{2, 1}, {64, 8}}) {
        doc["notification"]["pattern_spacing"] = geometry.first;
        doc["notification"]["pattern_stroke_width"] = geometry.second;
        assert(!dialog_appearance_read(doc.as<JsonObjectConst>(), restored));
        assert(restored.notification.pattern_spacing == geometry.first);
        assert(restored.notification.pattern_stroke_width == geometry.second);
    }
    for (const char* invalid : {
        R"({"notification":{"background_pattern":"unknown"}})",
        R"({"notification":{"background_pattern":1}})",
        R"({"notification":{"background_pattern_color":"[expr:1]"}})",
        R"({"notification":{"pattern_spacing":0}})",
        R"({"notification":{"pattern_spacing":1}})",
        R"({"notification":{"pattern_spacing":65}})",
        R"({"notification":{"pattern_spacing":2.5}})",
        R"({"notification":{"pattern_stroke_width":0}})",
        R"({"notification":{"pattern_stroke_width":9}})",
        R"({"notification":{"pattern_spacing":2,"pattern_stroke_width":3}})"}) {
        doc.clear();
        assert(!deserializeJson(doc, invalid));
        const DialogTemplates before = restored;
        assert(dialog_appearance_read(doc.as<JsonObjectConst>(), restored));
        assert(restored.notification.background_pattern == before.notification.background_pattern);
        assert(restored.notification.pattern_spacing == before.notification.pattern_spacing);
        assert(restored.notification.pattern_stroke_width == before.notification.pattern_stroke_width);
    }
    lv_obj_t* screen = lv_obj_create(nullptr);
    assert(screen);
    lv_screen_load(screen);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    for (uint8_t opacity : {100, 50, 0}) {
        appearance.notification.background_opacity = opacity;
        device_dialog_set_templates(appearance);
        assert(device_dialog_notify("Pattern", 0));
        auto* original = dialog_box();
        // Updating saved templates must not alter the live dialog's pattern or opacity.
        device_dialog_set_templates(DialogTemplates{});
        fail_allocation = true;
        assert(!device_dialog_notify("Failed replacement", 0));
        assert(dialog_box() == original);
        lv_obj_update_layout(lv_layer_top());
        lv_area_t bounds;
        lv_obj_get_coords(dialog_box(), &bounds);
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(display);
        auto pixel = [&](int x, int y) {
            lv_color32_t color;
            memcpy(&color, &pixels[(bounds.y1 + y) * 466 + bounds.x1 + x], sizeof(color));
            return color;
        };
        const auto base = pixel(3, 3);
        const auto stripe = pixel(3, 9);
        const int expected = opacity * 255 / 100;
        assert(abs(base.red - expected) <= 2 && base.green == 0 && base.blue == 0);
        assert(abs(stripe.green - expected) <= 2 && stripe.red == 0 && stripe.blue == 0);
        device_dialog_close();
    }
    appearance.confirmation.background_opacity = 50;
    assert(device_dialog_test(true, appearance.confirmation));
    assert(lv_obj_get_style_bg_opa(lv_obj_get_child(dialog_box(), -1), LV_PART_MAIN) == LV_OPA_COVER);
    lv_obj_update_layout(lv_layer_top());
    lv_refr_now(display);
    device_dialog_navigate("previous");
    device_dialog_navigate("tap");
    assert(!device_dialog_active());
    uint8_t count = 99;
    assert(action_continuation_take(nullptr, &count, nullptr, 0) == ACTION_CONTINUATION_SUCCESS && count == 0);
}

int main() {
    lv_init();
    lv_display_t* display = lv_display_create(466, 466);
    assert(display);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_ARGB8888);
    lv_display_set_buffers(display, pixels, nullptr, sizeof(pixels), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, [](lv_display_t* target, const lv_area_t*, uint8_t*) { lv_display_flush_ready(target); });
    device_typography_init("missing-on-bitmap-board", 30);
    const uint16_t medium = pad_font_pixel_size(pad_get_scale_info().font_medium);
    assert(pad_typography_font(pad_get_scale_info().font_small) == pad_nearest_bitmap_font(pad_font_pixel_size(pad_get_scale_info().font_small) * 30, medium));
    LabelStyle absolute{};
    absolute.font_size = 21;
    assert(pad_resolve_font(absolute, pad_get_scale_info().font_small) == &lv_font_montserrat_24);
    device_typography_init("@builtin", 0);
    {
        SplashScreen splash;
        splash.create();
        splash.show();
        for (const std::string& text : {std::string("Booting..."),
                                       std::string(1000, 'W'),
                                       std::string("Wi-Fi connected. Loading device and saved configuration.")}) {
            splash.setStatus(text.c_str());
            lv_obj_t* screen = lv_screen_active();
            lv_obj_update_layout(screen);
            assert(lv_obj_get_child_count(screen) == 3);
            for (int index = 0; index < 3; ++index)
                assert_inside_canvas(lv_obj_get_child(screen, index));
        }
    }
    for (uint8_t location : {0, 1, 2}) {
        DialogTemplates appearance;
        appearance.notification.position = location;
        appearance.notification.font_size = 36;
        device_dialog_set_templates(appearance);
        assert(device_dialog_notify(std::string(191, 'W').c_str(), 3000));
        lv_obj_t* layer = lv_layer_top();
        lv_obj_update_layout(layer);
        assert(lv_obj_get_child_count(layer) == 1);
        assert_inside_canvas(lv_obj_get_child(layer, 0));
        device_dialog_close();
        assert(lv_obj_get_child_count(layer) == 0);
    }
    device_dialog_set_templates(DialogTemplates{});
    dialog_lifecycle_checks();
    dialog_test_endpoint_checks();
    dialog_pattern_checks(display);
    wifi_setup_checks(display);
    printf("DialogAppearance=%zu DialogTemplates=%zu ButtonAction=%zu ActionPayload=%zu\n",
        sizeof(DialogAppearance), sizeof(DialogTemplates), sizeof(ButtonAction), sizeof(ActionPayload));
    puts("Dialog lifecycle, input, replacement, timeout, appearance checks passed");
    lv_display_delete(display);
    lv_deinit();
}
