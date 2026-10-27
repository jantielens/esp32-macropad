#include "tab5_dsi_driver.h"
#include BOARD_HARDWARE_HEADER
#include "../log_manager.h"
#include "tab5/vendor_panel_commands.h"

void Tab5DsiDriver::init() {
    if (!tab5_panel_revision()) {
        LOGE("Tab5", "Display unavailable: panel not identified");
        return;
    }
    MipiDsiDriver::init();
}

bool Tab5DsiDriver::isAvailable() const { return panel_handle != nullptr; }

void Tab5DsiDriver::setBacklightBrightness(uint8_t brightness) {
    if (isAvailable()) MipiDsiDriver::setBacklightBrightness(brightness);
}

void Tab5DsiDriver::configureLVGL(lv_display_t* display, uint8_t rotation) {
    if (isAvailable()) MipiDsiDriver::configureLVGL(display, rotation);
}

bool Tab5DsiDriver::asyncFlush() const { return isAvailable(); }

const mipi_dsi_init_cmd_t* Tab5DsiDriver::getInitCommands() const {
    return tab5_panel_revision() == 1 ? st7121_commands : st7123_commands;
}

size_t Tab5DsiDriver::getInitCommandCount() const {
    return tab5_panel_revision() == 1 ? sizeof(st7121_commands) / sizeof(st7121_commands[0])
                                      : sizeof(st7123_commands) / sizeof(st7123_commands[0]);
}

const char* Tab5DsiDriver::getLogTag() const { return "Tab5"; }

MipiDsiTimingConfig Tab5DsiDriver::getTimingConfig() const {
    if (tab5_panel_revision() == 1) return {70000000, 900, 2, 40, 40, 20, 24, 200, false};
    return {80000000, 1040, 2, 40, 40, 2, 8, 220, false};
}