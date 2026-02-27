/**
 * @file lv_conf.h
 * Configuration file for LVGL v9.5.0
 *
 * Rewritten from v8.4 for native v9 API.
 */

#ifndef LV_CONF_H
#define LV_CONF_H

/* Guard C-specific includes against the assembler (lv_blend_helium.S includes
   lv_conf_internal.h which pulls in this file). */
#ifndef __ASSEMBLY__

#include <stdint.h>

// lv_conf.h is included by LVGL's C sources.
// The project uses true/false-style feature flags (e.g., HAS_DISPLAY true) in board_config.h
// and board_overrides.h. In C, those require <stdbool.h> to make true/false available.
#include <stdbool.h>

// Pull in project feature flags (HAS_DISPLAY, HAS_TOUCH, etc.).
// board_config.h will include board_overrides.h when BOARD_HAS_OVERRIDE is set.
#include "board_config.h"

#endif /* __ASSEMBLY__ */

/*====================
   COLOR SETTINGS
 *====================*/

/* Color depth: 1 (1 byte per pixel), 8 (RGB332), 16 (RGB565), 32 (ARGB8888) */
#define LV_COLOR_DEPTH 16

/*=========================
   STDLIB SETTINGS
 *=========================*/

/*
 * Standard library selection for malloc, string, sprintf.
 * Options:
 *   LV_STDLIB_BUILTIN      - LVGL's built-in TLSF allocator
 *   LV_STDLIB_CLIB         - Standard C functions
 *   LV_STDLIB_CUSTOM       - User-provided implementations
 *
 * Custom malloc: project provides lv_malloc_core / lv_realloc_core / lv_free_core
 * via lvgl_heap.cpp (routes through ESP32 heap_caps for PSRAM-first allocation).
 */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_CUSTOM
#define LV_USE_STDLIB_STRING    LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_CLIB

/*=========================
   HAL / TIMING SETTINGS
 *=========================*/

/* Default display refresh period [ms].
 * Board overrides can set LVGL_REFR_PERIOD_MS for per-board tuning.
 * This is the compile-time default; runtime override uses lv_display_set_refr_period(). */
#define LV_DEF_REFR_PERIOD 33

/* Input device read period [ms] */
#define LV_DEF_INDEV_READ_PERIOD 10

/*=========================
   OPERATING SYSTEM
 *=========================*/

/* LV_OS_NONE: LVGL does not create internal render threads.
 * Thread safety is managed by DisplayManager's manual FreeRTOS mutex. */
#define LV_USE_OS   LV_OS_NONE

/*=========================
   DRAW / RENDERING
 *=========================*/

/* Draw buffer stride alignment (bytes).
 * Must be 1 when LV_USE_PPA is enabled: the ESP-IDF PPA driver computes row
 * stride from pic_w * bpp (no padding).  A value >1 that adds padding causes
 * PPA to write at wrong offsets, producing horizontal-line artefacts.
 * Buffer *start* alignment is handled by LV_DRAW_BUF_ALIGN (64) below. */
#define LV_DRAW_BUF_STRIDE_ALIGN   1

/* Draw buffer start address alignment (bytes).
 * 64 = ESP32-P4 L2 cache line size — required for PPA and optimal DMA. */
#define LV_DRAW_BUF_ALIGN          64

/* Software renderer */
#define LV_USE_DRAW_SW 1
#if LV_USE_DRAW_SW == 1
    /* Number of SW draw units.  1 = inline rendering (no thread dispatch).
     * Multi-unit threading regressed FPS on P4 (PSRAM bandwidth bottleneck). */
    #define LV_DRAW_SW_DRAW_UNIT_CNT    1

    /* Enable complex drawing (shadows, gradients, etc.) */
    #define LV_DRAW_SW_COMPLEX          1

    /* Allow indexed image pixel format support */
    #define LV_DRAW_SW_SUPPORT_INDEXED_IMAGE 0
#endif

/* ESP32-P4 PPA (Pixel Processing Accelerator) — hardware-accelerated fills and blits.
 * Requires SOC_PPA_SUPPORTED (ESP32-P4 only) and cache-line-aligned buffers. */
#if __has_include("soc/soc_caps.h")
    #include "soc/soc_caps.h"
#endif
#if defined(SOC_PPA_SUPPORTED) && SOC_PPA_SUPPORTED
    #define LV_USE_PPA 1
#else
    #define LV_USE_PPA 0
#endif

/* Bridge for ESP-IDF Kconfig check in PPA driver (lv_draw_ppa_private.h).
 * The PPA code checks CONFIG_LV_DRAW_BUF_ALIGN directly; without lv_conf.h
 * going through Kconfig this symbol would be undefined.
 * Only needed when PPA is actually enabled. */
#if LV_USE_PPA
    #ifndef CONFIG_LV_DRAW_BUF_ALIGN
    #define CONFIG_LV_DRAW_BUF_ALIGN   LV_DRAW_BUF_ALIGN
    #endif
#endif

/*================
 * LOGGING
 *===============*/

#define LV_USE_LOG 0

/*================
 * ASSERTS
 *===============*/

#define LV_USE_ASSERT_NULL          1   /* Check if the parameter is NULL */
#define LV_USE_ASSERT_MALLOC        1   /* Check if memory allocation succeeded */
#define LV_USE_ASSERT_STYLE         0   /* Check style consistency */
#define LV_USE_ASSERT_MEM_INTEGRITY 0   /* Check memory integrity (slow) */
#define LV_USE_ASSERT_OBJ           0   /* Check object type and existence (slow) */

/*================
 * FONT USAGE
 *===============*/

/* Montserrat fonts with various styles */
#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 0
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 0
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 0
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 0

/* Demonstrate special features */
#define LV_FONT_MONTSERRAT_12_SUBPX      0
#define LV_FONT_MONTSERRAT_28_COMPRESSED 0
#define LV_FONT_DEJAVU_16_PERSIAN_HEBREW 0
#define LV_FONT_SIMSUN_16_CJK            0

/* Pixel perfect monospace fonts */
#define LV_FONT_UNSCII_8  0
#define LV_FONT_UNSCII_16 0

/* Optionally declare custom fonts here */
#define LV_FONT_CUSTOM_DECLARE

/* Set a default font */
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/*=================
 *  TEXT SETTINGS
 *=================*/

/* Select a character encoding for strings */
#define LV_TXT_ENC LV_TXT_ENC_UTF8

/* Support bidirectional texts */
#define LV_USE_BIDI 0

/* Support Arabic/Persian processing */
#define LV_USE_ARABIC_PERSIAN_CHARS 0

/*===================
 *  WIDGET USAGE
 *==================*/

#define LV_USE_ARC        1
#define LV_USE_BAR        0
#define LV_USE_BUTTON     0
#define LV_USE_BUTTONMATRIX  0
#define LV_USE_CALENDAR   0
#ifndef LV_USE_CANVAS
    #if HAS_TOUCH
        #define LV_USE_CANVAS 1
    #else
        #define LV_USE_CANVAS 0
    #endif
#endif
#define LV_USE_CHART      0
#define LV_USE_CHECKBOX   0
#define LV_USE_DROPDOWN   0

/* Image widget: required by splash screen (PNG logo).
 * Enable for all display-capable boards. */
#if HAS_DISPLAY
    #ifndef LV_USE_IMAGE
        #define LV_USE_IMAGE    1
    #endif
#else
    #ifndef LV_USE_IMAGE
        #define LV_USE_IMAGE    0
    #endif
#endif

#define LV_USE_ANIMIMG    0  /* Requires LV_USE_IMAGE; we don't use animated images */
#define LV_USE_IMAGEBUTTON   0
#define LV_USE_KEYBOARD  0
#define LV_USE_LABEL      1
#if LV_USE_LABEL
    #define LV_LABEL_TEXT_SELECTION 0
    #define LV_LABEL_LONG_TXT_HINT 0
#endif
#define LV_USE_LED        0
#define LV_USE_LINE       0
#define LV_USE_LIST       0
#define LV_USE_MENU       0
#define LV_USE_MSGBOX     0
#define LV_USE_ROLLER     0
#define LV_USE_SCALE      0
#define LV_USE_SLIDER     0
#define LV_USE_SPAN       0
#define LV_USE_SPINBOX    0
#define LV_USE_SPINNER    1
#define LV_USE_SWITCH     0
#define LV_USE_TABLE      0
#define LV_USE_TABVIEW    0
#define LV_USE_TEXTAREA   0
#define LV_USE_TILEVIEW   0
#define LV_USE_WIN        0
#define LV_USE_ARCLABEL   0

/*==================
 * THEMES
 *==================*/

/* A simple, impressive and very complete theme */
#define LV_USE_THEME_DEFAULT 1
#if LV_USE_THEME_DEFAULT
    /* 0: Light mode; 1: Dark mode */
    #define LV_THEME_DEFAULT_DARK 1

    /* 1: Enable grow on press */
    #define LV_THEME_DEFAULT_GROW 1

    /* Default transition time in [ms] */
    #define LV_THEME_DEFAULT_TRANSITION_TIME 80
#endif

/* Simple theme (good starting point for custom themes) */
#define LV_USE_THEME_SIMPLE 0

/* A theme designed for monochrome displays */
#define LV_USE_THEME_MONO 0

/*==================
 * LAYOUTS
 *==================*/

/* A layout similar to Flexbox in CSS */
#define LV_USE_FLEX 0

/* A layout similar to Grid in CSS */
#define LV_USE_GRID 0

/*==================
 * OTHERS / EXTRAS
 *==================*/

/* System monitor */
#define LV_USE_SYSMON 0

/* Object ID (for testing frameworks) */
#define LV_USE_OBJ_ID 0

/* IME - Input Method Editor (Pinyin input) */
#define LV_USE_IME_PINYIN 0

/* File explorer */
#define LV_USE_FILE_EXPLORER 0

/* Grid navigation */
#define LV_USE_GRIDNAV 0

/* Fragment (UI component lifecycle manager) */
#define LV_USE_FRAGMENT 0

/* Snapshot (capture widget to image) */
#define LV_USE_SNAPSHOT 0

/* Monkey test */
#define LV_USE_MONKEY 0

/* Profiler */
#define LV_USE_PROFILER 0

/* Barcode */
#define LV_USE_BARCODE 0

/* QR code */
#define LV_USE_QRCODE 0

/* Tiny TTF font engine */
#define LV_USE_TINY_TTF 0

/* FreeType font engine */
#define LV_USE_FREETYPE 0

/* Built-in image decoders */
#define LV_USE_LODEPNG 0
#define LV_USE_LIBPNG 0
#define LV_USE_BMP 0
#define LV_USE_TJPGD 0
#define LV_USE_LIBJPEG_TURBO 0
#define LV_USE_GIF 0

/* SVG support */
#define LV_USE_SVG 0
#define LV_USE_SVG_ANIMATION 0

/* Vector graphics */
#define LV_USE_VECTOR_GRAPHIC 0

/* LottiePlayer */
#define LV_USE_LOTTIE 0

/* RLE image compression */
#define LV_USE_RLE 0

/* File system interfaces */
#define LV_USE_FS_STDIO  0
#define LV_USE_FS_POSIX  0
#define LV_USE_FS_WIN32  0
#define LV_USE_FS_FATFS  0
#define LV_USE_FS_MEMFS  0
#define LV_USE_FS_LITTLEFS 0
#define LV_USE_FS_ARDUINO_ESP_LITTLEFS 0
#define LV_USE_FS_ARDUINO_SD 0

/*==================
 *  COMPILER SETTINGS
 *==================*/

/* For big endian systems set to 1 */
#define LV_BIG_ENDIAN_SYSTEM 0

/* Define a custom attribute to `lv_tick_inc` function */
#define LV_ATTRIBUTE_TICK_INC

/* Define a custom attribute to `lv_timer_handler` function */
#define LV_ATTRIBUTE_TIMER_HANDLER

/* Define a custom attribute to `lv_display_flush_ready` function */
#define LV_ATTRIBUTE_FLUSH_READY

/* Required alignment size for buffers */
#define LV_ATTRIBUTE_MEM_ALIGN_SIZE 1

/* Will be added where memories needs to be aligned */
#define LV_ATTRIBUTE_MEM_ALIGN

/* Attribute to mark large constant arrays for example font's bitmaps */
#define LV_ATTRIBUTE_LARGE_CONST

/* Attribute to mark large RAM arrays */
#define LV_ATTRIBUTE_LARGE_RAM_ARRAY

/* Prefix performance critical functions to place them into a faster memory (e.g RAM)
 * ESP32-P4: place hot LVGL functions in IRAM for faster execution.
 * Classic ESP32/C3: IRAM is too small — leave empty to avoid overflow. */
#if defined(CONFIG_IDF_TARGET_ESP32P4) || defined(CONFIG_IDF_TARGET_ESP32S3)
    #define LV_ATTRIBUTE_FAST_MEM IRAM_ATTR
#else
    #define LV_ATTRIBUTE_FAST_MEM
#endif

/* Export integer constant to binding */
#define LV_EXPORT_CONST_INT(int_value) struct _silence_gcc_warning

/* Attribute for extern data */
#define LV_ATTRIBUTE_EXTERN_DATA

#endif /*LV_CONF_H*/
