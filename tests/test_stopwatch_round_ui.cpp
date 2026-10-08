#include <cassert>
#include <cstring>
#include <string>
#include <lvgl.h>

#include "log_manager.h"

void log_write(LogLevel, const char*, const char*, ...) {}
bool log_diagnostics_enabled(const char*) { return false; }

#define DISPLAY_MANAGER_H
class DisplayManager {
public:
    int getActiveWidth() { return 466; }
    int getActiveHeight() { return 466; }
    void lock() {}
    void unlock() {}
};
static DisplayManager manager;
DisplayManager* displayManager = &manager;

#include "../src/app/screens/splash_screen.cpp"
#include "../src/app/message_bubble.cpp"

static void assert_inside_circle(lv_obj_t* object) {
    lv_area_t area;
    lv_obj_get_coords(object, &area);
    assert(area.x1 >= 68 && area.y1 >= 68 && area.x2 < 397 && area.y2 < 397);
    for (int x : {area.x1, area.x2}) {
        for (int y : {area.y1, area.y2}) {
            const int dx = 2 * x - 465;
            const int dy = 2 * y - 465;
            assert(dx * dx + dy * dy <= 466 * 466);
        }
    }
}

int main() {
    lv_init();
    lv_display_t* display = lv_display_create(466, 466);
    assert(display);
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
                assert_inside_circle(lv_obj_get_child(screen, index));
        }
    }
    MessageBubbleParams params{};
    memset(params.text, 'W', sizeof(params.text) - 1);
    params.font_size = 48;
    params.opacity = 100;
    for (uint8_t location : {NOTIFY_LOC_TOP, NOTIFY_LOC_CENTER, NOTIFY_LOC_BOTTOM}) {
        params.location = location;
        message_bubble_show(&params);
        message_bubble_loop();
        lv_obj_t* layer = lv_layer_top();
        lv_obj_update_layout(layer);
        assert(lv_obj_get_child_count(layer) == 1);
        assert_inside_circle(lv_obj_get_child(layer, 0));
        message_bubble_dismiss();
        message_bubble_loop();
        assert(lv_obj_get_child_count(layer) == 0);
    }
    lv_display_delete(display);
    lv_deinit();
}
