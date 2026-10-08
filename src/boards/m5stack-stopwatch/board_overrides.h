#ifndef BOARD_OVERRIDES_M5STACK_STOPWATCH_H
#define BOARD_OVERRIDES_M5STACK_STOPWATCH_H

// M5Stack StopWatch: ESP32-S3R8, 16 MiB flash, 8 MiB OPI PSRAM.
// Reference: https://docs.m5stack.com/en/core/StopWatch
// Power/reset pins are M5IOE1 outputs, not ESP32 GPIO numbers.
#define HAS_M5STACK_STOPWATCH true
#define HAS_DISPLAY true
#define HAS_TOUCH true
#define HAS_BACKLIGHT true
#define HAS_BUILTIN_LED false

// Preserve internal/DMA RAM for display, Wi-Fi, the portal, and OTA.
#define HAS_BLE false
#define HAS_BLE_HID false
#define HAS_USB_HID false
#define HAS_NATIVE_EXTENSIONS false
#define HAS_MCP false
#define HAS_IMAGE_FETCH false
#define HAS_HA_HISTORY false
#define HAS_MUSIC_ANALYSIS false
#define HAS_AUDIO_INPUT false
#define HAS_ES7210_MIC false
#define HAS_REMOTE_LOG true

// Driver Selection (HAL)
#define DISPLAY_DRIVER DISPLAY_DRIVER_ARDUINO_GFX_CO5300
#define DISPLAY_PANEL "CO5300"
#define TOUCH_DRIVER TOUCH_DRIVER_CST820B_WIRE
#define DISPLAY_SHAPE DISPLAY_SHAPE_ROUND
#define UI_SCALE_TIER UI_SCALE_MEDIUM
#define DISPLAY_WIDTH 466
#define DISPLAY_HEIGHT 466
#define DISPLAY_ROTATION 0
#define LV_USE_PERF_MONITOR_POS LV_ALIGN_BOTTOM_MID
#define LVGL_BUFFER_PREFER_INTERNAL false
#define LVGL_BUFFER_SIZE (DISPLAY_WIDTH * 16)
#define MAX_PAD_BUTTONS 25
#define MAX_GRID_COLS 5
#define MAX_GRID_ROWS 5
#define SCREEN_HISTORY_MAX 3
#define MAX_MQTT_TRIGGERS 3
#define ACTION_CONTINUATION_SLOTS 1

// AMOLED brightness is a panel command. There is no backlight PWM GPIO.
#define LCD_BL_PIN -1
#define LCD_QSPI_RST -1
#define LCD_QSPI_CS 39
#define LCD_QSPI_PCLK 40
#define LCD_QSPI_D0 41
#define LCD_QSPI_D1 42
#define LCD_QSPI_D2 46
#define LCD_QSPI_D3 45
#define TFT_SPI_FREQ_HZ (40 * 1000 * 1000)

// One shared internal bus, initialized by the board hardware module.
#define TOUCH_I2C_SDA 47
#define TOUCH_I2C_SCL 48
#define TOUCH_INT 13
#define TOUCH_RST -1
#define SENSOR_I2C_SDA 47
#define SENSOR_I2C_SCL 48
#define SENSOR_I2C_FREQUENCY 100000

// Both programmable buttons use the existing tap/hold action dispatcher.
// Hold yellow during startup for configuration-mode recovery.
#define HAS_BUTTON true
#define HAS_CONFIG_MODE_BUTTON true
#define BUTTON_PIN 2
#define BUTTON_ACTIVE_LOW true
#define NUM_HW_BUTTONS 2
#ifdef __cplusplus
static constexpr HwButtonDef HW_BUTTON_DEFS[NUM_HW_BUTTONS] = {
    { .pin = 2, .active_low = true, .label = "Yellow" },
    { .pin = 1, .active_low = true, .label = "Blue" },
};
#endif

// Milestone 2: opt in only after measuring combined-load memory headroom.
#ifndef HAS_AUDIO
#define HAS_AUDIO false
#endif
#define AUDIO_OUTPUT_DRIVER AUDIO_OUTPUT_DRIVER_ES8311
#define AUDIO_CODEC_ADDR 0x18
#define AUDIO_I2S_MCLK 18
#define AUDIO_I2S_BCLK 17
#define AUDIO_I2S_LRCK 15
#define AUDIO_I2S_DOUT 21
#define AUDIO_I2S_DIN -1
#define AUDIO_PA_PIN -1
#define AUDIO_SAMPLE_RATE 48000
#define AUDIO_MP3_SCRATCH_PSRAM true
#define AUDIO_TASK_STACK_SIZE 24576
#define AUDIO_DEFAULT_VOLUME 30

#endif // BOARD_OVERRIDES_M5STACK_STOPWATCH_H
