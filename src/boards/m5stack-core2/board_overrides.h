#ifndef BOARD_OVERRIDES_M5STACK_CORE2_H
#define BOARD_OVERRIDES_M5STACK_CORE2_H

#define BOARD_HARDWARE_SOURCE "m5stack_core2_hardware.cpp"
#define BOARD_HARDWARE_HEADER "m5stack_core2_hardware.h"
#define BOARD_HARDWARE_LOOP m5stack_core2_loop
#define BOARD_AUDIO_CONTROL_FACTORY m5stack_core2_audio_control
#define BOARD_SENSOR_SOURCE "m5stack_core2_battery_sensor.cpp"
#define BOARD_ACTION_SOURCE "m5stack_core2_actions.cpp"
#define ACTION_TYPE_REGISTRY_MAX_TYPES 26
#define ACTION_CONTINUATION_SLOTS 1

#define HAS_DISPLAY true
#define HAS_TOUCH true
#define HAS_BACKLIGHT true
#define HAS_AUDIO true
#define HAS_AUDIO_INPUT false
#define HAS_MCP false
#define HAS_BLE_HID false
#define HAS_USB_HID false
#define HAS_IMAGE_FETCH false
#define HAS_HA_HISTORY false
#define HAS_HA_LIVE false
#define HAS_CONFIG_MODE_BUTTON false

#define DISPLAY_DRIVER DISPLAY_DRIVER_TFT_ESPI
#define TOUCH_DRIVER TOUCH_DRIVER_FT6336
#define DISPLAY_SHAPE DISPLAY_SHAPE_RECT
#define UI_SCALE_TIER UI_SCALE_SMALL
#define DISPLAY_WIDTH 320
#define DISPLAY_HEIGHT 240
#define DISPLAY_ROTATION 1
#define DISPLAY_PANEL "ILI9342"
#define TOUCH_I2C_BUS 0
#define TOUCH_I2C_SDA 21
#define TOUCH_I2C_SCL 22
// Enable the panel's required display inversion.
#define DISPLAY_INVERSION_ON true
// Route backlight brightness through the AXP192 PMIC instead of GPIO PWM.
#define BACKLIGHT_VIA_PMIC true
// Classic ESP32 requires internal memory for static FreeRTOS task stacks.
#define LVGL_TASK_USE_PSRAM_STACK false

// Select TFT_eSPI's ILI9341-compatible initialization for the ILI9342 panel.
#define ILI9341_DRIVER
// Apply the M5Stack-specific TFT_eSPI rotation and color handling.
#define M5STACK
// TFT SPI data from the panel to the ESP32.
#define TFT_MISO 38
// TFT SPI data from the ESP32 to the panel.
#define TFT_MOSI 23
// TFT SPI clock signal.
#define TFT_SCLK 18
// TFT chip-select signal.
#define TFT_CS 5
// TFT data/command select signal.
#define TFT_DC 15
// Reset is controlled by AXP192 GPIO4, not a direct TFT_eSPI GPIO.
#define TFT_RST -1
// TFT_eSPI SPI write clock frequency in hertz.
#define SPI_FREQUENCY 40000000
// TFT_eSPI SPI read clock frequency in hertz.
#define SPI_READ_FREQUENCY 20000000

#define AUDIO_OUTPUT_DRIVER AUDIO_OUTPUT_DRIVER_NS4168
#define AUDIO_I2S_BCLK 12
#define AUDIO_I2S_LRCK 0
#define AUDIO_I2S_DOUT 2
#define AUDIO_I2S_MCLK -1
#define AUDIO_I2S_DIN -1
#define AUDIO_PA_PIN -1
#define AUDIO_DEFAULT_VOLUME 50

#define HAS_BUTTON true
#define NUM_HW_BUTTONS 3
#ifdef __cplusplus
static constexpr HwButtonDef HW_BUTTON_DEFS[NUM_HW_BUTTONS] = {
    { .pin = HW_BUTTON_VIRTUAL_PIN, .active_low = false, .label = "A" },
    { .pin = HW_BUTTON_VIRTUAL_PIN, .active_low = false, .label = "B" },
    { .pin = HW_BUTTON_VIRTUAL_PIN, .active_low = false, .label = "C" },
};
#endif

#endif