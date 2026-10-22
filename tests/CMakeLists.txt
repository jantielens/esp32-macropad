include(FetchContent)
include(GoogleTest)
include(CMakeParseArguments)

add_custom_target(widget_manifest ALL
    COMMAND python3 ${PROJECT_SOURCE_DIR}/tools/widget_manifest.py
        --firmware ${CMAKE_CURRENT_BINARY_DIR}/widget_modules.h
    BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/widget_modules.h
    VERBATIM)

add_test(NAME test_log_manager COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/test_log_manager.py)
add_test(NAME test_portal_preflight COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/test_portal_preflight.py)
add_test(NAME test_stopwatch_hal COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/test_stopwatch_hal.py)
add_test(NAME test_log_manager_serial COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/test_log_manager_serial.py)
add_test(NAME test_image_fetch_client COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/test_image_fetch_client.py)
add_test(NAME test_board_feature_overrides COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/test_board_feature_overrides.py)
add_test(NAME test_remote_log_runtime COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/test_remote_log_runtime.py)
add_test(NAME test_firmware_size_report COMMAND python3 ${PROJECT_SOURCE_DIR}/tests/test_firmware_size_report.py)
add_test(NAME test_config_identifier_guards COMMAND python3
    ${PROJECT_SOURCE_DIR}/tests/test_config_identifier_guards.py --compiler ${CMAKE_CXX_COMPILER})

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.16.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(googletest)

function(add_host_gtest target)
    add_executable(${target} ${ARGN})
    target_compile_features(${target} PRIVATE cxx_std_17)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
    target_include_directories(${target} PRIVATE
        ${PROJECT_SOURCE_DIR}/tests
        ${PROJECT_SOURCE_DIR}/src/app
    )
    target_link_libraries(${target} PRIVATE GTest::gtest_main)
    gtest_discover_tests(${target})
endfunction()

function(add_host_legacy_test target)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEFINITIONS;INCLUDES;LIBRARIES;OPTIONS;PREINCLUDE" ${ARGN})
    add_executable(${target} ${ARG_SOURCES})
    target_compile_features(${target} PRIVATE cxx_std_17)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror ${ARG_OPTIONS})
    target_include_directories(${target} PRIVATE
        ${ARG_INCLUDES}
        ${PROJECT_SOURCE_DIR}/tests
        ${PROJECT_SOURCE_DIR}/src/app
    )
    target_compile_definitions(${target} PRIVATE ${ARG_DEFINITIONS})
    target_link_libraries(${target} PRIVATE ${ARG_LIBRARIES})
    foreach(header IN LISTS ARG_PREINCLUDE)
        target_compile_options(${target} PRIVATE "SHELL:-include ${header}")
    endforeach()
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endfunction()

function(add_host_command_test name)
    add_test(NAME ${name} COMMAND ${ARGN})
    set_tests_properties(${name} PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endfunction()

set(HOST_ARDUINOJSON_DIR "$ENV{HOME}/Arduino/libraries/ArduinoJson/src")
set(HOST_LVGL_DIR "$ENV{HOME}/Arduino/libraries/lvgl/src")

add_host_legacy_test(test_crowpanel_epaper_driver
    SOURCES test_crowpanel_epaper_driver.cpp ${PROJECT_SOURCE_DIR}/src/app/drivers/crowpanel42_epaper_driver.cpp
    INCLUDES ${PROJECT_SOURCE_DIR}/tests/crowpanel_driver_test_overrides ${HOST_LVGL_DIR} ${HOST_ARDUINOJSON_DIR}
        ${PROJECT_SOURCE_DIR}/src/boards/crowpanel-4.2-interactive
    DEFINITIONS BOARD_HAS_OVERRIDE=1 BOARD_HAS_PSRAM=1 CONFIG_IDF_TARGET_ESP32S3=1 TEST_HEAP_CAPS_ALLOCATOR=1
        LV_CONF_SKIP=1 LV_USE_OS=0 LV_USE_LOG=0 TEST_FREERTOS_MUTEX_LIFECYCLE=1
    OPTIONS -Wno-unused-parameter
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/log_manager.h
)

foreach(board waveshare-7.8-interactive reterminal-e1003-interactive)
  foreach(diagnostics 0 1)
    set(diagnostic_suffix "")
    if(diagnostics)
        set(diagnostic_suffix "_diagnostics")
    endif()
    add_host_legacy_test(test_it8951_${board}${diagnostic_suffix}
        SOURCES test_it8951_epaper_driver.cpp ${PROJECT_SOURCE_DIR}/src/app/drivers/it8951_epaper_driver.cpp
        INCLUDES ${PROJECT_SOURCE_DIR}/tests/it8951_driver_test_overrides ${HOST_LVGL_DIR} ${HOST_ARDUINOJSON_DIR}
            ${PROJECT_SOURCE_DIR}/src/boards/${board}
        DEFINITIONS HAS_DISPLAY_DIAGNOSTICS=${diagnostics} BOARD_HAS_OVERRIDE=1 BOARD_HAS_PSRAM=1 CONFIG_IDF_TARGET_ESP32S3=1
            TEST_HEAP_CAPS_ALLOCATOR=1 TEST_FREERTOS_MUTEX_LIFECYCLE=1 LV_CONF_SKIP=1 LV_USE_OS=0 LV_USE_LOG=0
        OPTIONS -Wno-unused-parameter
        PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/log_manager.h ${PROJECT_SOURCE_DIR}/tests/freertos/portmacro.h
    )
  endforeach()
endforeach()

add_host_legacy_test(test_sensor_manager
    SOURCES test_sensor_manager.cpp ${PROJECT_SOURCE_DIR}/src/app/binding_value.cpp
    INCLUDES ${HOST_ARDUINOJSON_DIR}
    OPTIONS -Wno-deprecated-declarations
)
add_host_command_test(test_sensor_manager_duty_cycle $<TARGET_FILE:test_sensor_manager> --duty-cycle)

file(READ "${PROJECT_SOURCE_DIR}/src/app/web_portal_component_api.cpp" PORTAL_ROUTE_SOURCE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/app/web_portal_component_api.cpp")
string(FIND "${PORTAL_ROUTE_SOURCE}" "static void handleComponentGetConfig(" PORTAL_ROUTE_START)
string(SUBSTRING "${PORTAL_ROUTE_SOURCE}" ${PORTAL_ROUTE_START} -1 PORTAL_ROUTE_HANDLERS)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/portal_route_handlers.inc" "${PORTAL_ROUTE_HANDLERS}")
file(READ "${PROJECT_SOURCE_DIR}/src/app/web_portal_fs_store.cpp" PORTAL_STORE_SOURCE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/app/web_portal_fs_store.cpp")
string(FIND "${PORTAL_STORE_SOURCE}" "void fs_indexed_store_register_routes(" PORTAL_STORE_START)
string(FIND "${PORTAL_STORE_SOURCE}" "#endif // HAS_DISPLAY" PORTAL_STORE_END)
math(EXPR PORTAL_STORE_LENGTH "${PORTAL_STORE_END} - ${PORTAL_STORE_START}")
string(SUBSTRING "${PORTAL_STORE_SOURCE}" ${PORTAL_STORE_START} ${PORTAL_STORE_LENGTH} PORTAL_STORE_HANDLERS)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/portal_store_handlers.inc" "${PORTAL_STORE_HANDLERS}")
add_host_legacy_test(test_web_portal_path SOURCES test_web_portal_path.cpp)
target_include_directories(test_web_portal_path PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
add_host_legacy_test(test_alarm_clock_core SOURCES test_alarm_clock_core.cpp)
add_host_legacy_test(test_alarm_dispatch_lock
    SOURCES test_alarm_dispatch_lock.cpp
    INCLUDES ${HOST_ARDUINOJSON_DIR}
    OPTIONS -Wno-deprecated-declarations
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/board_config.h ${PROJECT_SOURCE_DIR}/tests/log_manager.h)
add_host_legacy_test(test_navigation_wake
    SOURCES test_navigation_wake.cpp ${PROJECT_SOURCE_DIR}/src/app/pad_cycle.cpp
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/board_config.h ${PROJECT_SOURCE_DIR}/tests/log_manager.h
)
set(NAVIGATION_LOCK_SOURCE "${PROJECT_SOURCE_DIR}/src/app/display_manager.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${NAVIGATION_LOCK_SOURCE}")
file(READ "${NAVIGATION_LOCK_SOURCE}" NAVIGATION_LOCK_SOURCE_TEXT)
string(FIND "${NAVIGATION_LOCK_SOURCE_TEXT}" "void DisplayManager::lockIfNeeded(" NAVIGATION_LOCK_START)
string(FIND "${NAVIGATION_LOCK_SOURCE_TEXT}" "void DisplayManager::lock()" NAVIGATION_LOCK_END)
if(NAVIGATION_LOCK_START LESS 0 OR NAVIGATION_LOCK_END LESS NAVIGATION_LOCK_START)
    message(FATAL_ERROR "Navigation test could not locate display lock helpers")
endif()
math(EXPR NAVIGATION_LOCK_LENGTH "${NAVIGATION_LOCK_END} - ${NAVIGATION_LOCK_START}")
string(SUBSTRING "${NAVIGATION_LOCK_SOURCE_TEXT}" ${NAVIGATION_LOCK_START}
    ${NAVIGATION_LOCK_LENGTH} NAVIGATION_LOCK_HELPERS)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/navigation_lock_helpers.inc" "${NAVIGATION_LOCK_HELPERS}")
target_include_directories(test_navigation_wake PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
add_host_legacy_test(test_alarm_calendar SOURCES test_alarm_calendar.cpp
    ${PROJECT_SOURCE_DIR}/src/app/time_service.cpp ${PROJECT_SOURCE_DIR}/src/app/binding_template.cpp stubs.cpp
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/board_config.h ${PROJECT_SOURCE_DIR}/tests/log_manager.h
    OPTIONS -pthread)

add_host_gtest(test_mousepad test_mousepad.cpp)
add_host_legacy_test(test_remote_log SOURCES test_remote_log.cpp)
add_host_gtest(test_gamepad test_gamepad.cpp)
add_host_gtest(test_web_portal_timing test_web_portal_timing.cpp)

enable_language(C)
file(GLOB_RECURSE HOST_LVGL_SOURCES CONFIGURE_DEPENDS
    "${HOST_LVGL_DIR}/*.c" "${HOST_LVGL_DIR}/*.cpp")
add_library(host_lvgl STATIC EXCLUDE_FROM_ALL ${HOST_LVGL_SOURCES})
target_include_directories(host_lvgl PUBLIC ${HOST_LVGL_DIR})
target_compile_definitions(host_lvgl PUBLIC LV_CONF_SKIP=1 LV_USE_OS=0 LV_USE_LOG=0
    LV_MEM_SIZE=1048576
    LV_USE_TINY_TTF=1 LV_TINY_TTF_FILE_SUPPORT=0
    LV_TINY_TTF_CACHE_GLYPH_CNT=16 LV_TINY_TTF_CACHE_KERNING_CNT=16
    LV_FONT_MONTSERRAT_12=1 LV_FONT_MONTSERRAT_18=1 LV_FONT_MONTSERRAT_24=1
    LV_FONT_MONTSERRAT_32=1 LV_FONT_MONTSERRAT_36=1)
add_host_legacy_test(test_runtime_ttf_oom
    SOURCES test_runtime_ttf_oom.cpp
    LIBRARIES host_lvgl m
)
target_link_options(test_runtime_ttf_oom PRIVATE
    "-Wl,--wrap=lv_malloc_core" "-Wl,--wrap=lv_realloc_core")
set_tests_properties(test_runtime_ttf_oom PROPERTIES TIMEOUT 15)
add_host_legacy_test(test_font_store
    SOURCES test_font_store.cpp ${PROJECT_SOURCE_DIR}/src/app/font_store.cpp
    DEFINITIONS HAS_TTF_FONTS=0
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/board_config.h
)
add_host_legacy_test(test_pad_font_cache
    SOURCES test_pad_font_cache.cpp ${PROJECT_SOURCE_DIR}/src/app/pad_font_cache.cpp
    DEFINITIONS HAS_TTF_FONTS=1 HAS_FREETYPE_FONTS=0 HOST_LOG_CAPTURE
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/board_config.h ${PROJECT_SOURCE_DIR}/tests/font_runtime_overrides.h ${PROJECT_SOURCE_DIR}/tests/log_manager.h
    LIBRARIES host_lvgl m
)
set_tests_properties(test_pad_font_cache PROPERTIES TIMEOUT 15)
file(GLOB HOST_FREETYPE_SOURCES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/libraries/MacropadFreeType/src/automono_*.c")
add_library(host_freetype STATIC EXCLUDE_FROM_ALL ${HOST_FREETYPE_SOURCES})
target_include_directories(host_freetype PUBLIC
    "${PROJECT_SOURCE_DIR}/libraries/MacropadFreeType/src")
foreach(diagnostics 0 1)
set(font_cache_target test_pad_font_cache_freetype)
if(diagnostics)
    string(APPEND font_cache_target "_diagnostics")
endif()
add_host_legacy_test(${font_cache_target}
    SOURCES test_pad_font_cache.cpp ${PROJECT_SOURCE_DIR}/src/app/pad_font_cache.cpp
        ${PROJECT_SOURCE_DIR}/src/app/freetype_mono_font.cpp
    DEFINITIONS HAS_DISPLAY_DIAGNOSTICS=${diagnostics} HAS_TTF_FONTS=1 HAS_FREETYPE_FONTS=1 HOST_LOG_CAPTURE
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/board_config.h ${PROJECT_SOURCE_DIR}/tests/font_runtime_overrides.h ${PROJECT_SOURCE_DIR}/tests/log_manager.h
    LIBRARIES host_lvgl host_freetype m
)
set_tests_properties(${font_cache_target} PROPERTIES TIMEOUT 15)
target_link_options(${font_cache_target} PRIVATE
    "-Wl,--wrap=lv_malloc_core" "-Wl,--wrap=lv_realloc_core")
target_compile_definitions(${font_cache_target} PRIVATE
    TEST_UNICODE_FONT_PATH="${PROJECT_SOURCE_DIR}/tests/fixtures/DevanagariTest.ttf")
endforeach()
foreach(disabled_font_feature IN ITEMS HAS_DISPLAY HAS_TTF_FONTS HAS_FREETYPE_FONTS)
    add_test(NAME test_freetype_disabled_${disabled_font_feature}
        COMMAND ${CMAKE_CXX_COMPILER} -DBOARD_CONFIG_H
            -DHAS_DISPLAY=1 -DHAS_TTF_FONTS=1 -DHAS_FREETYPE_FONTS=1
            -U${disabled_font_feature} -D${disabled_font_feature}=0
            -fsyntax-only ${PROJECT_SOURCE_DIR}/src/app/freetype_mono_font.cpp)
endforeach()
foreach(font_backend IN ITEMS internal sd)
    if(font_backend STREQUAL "sd")
        set(font_sd 1)
    else()
        set(font_sd 0)
    endif()
    add_host_legacy_test(test_font_store_${font_backend}
        SOURCES test_font_store.cpp ${PROJECT_SOURCE_DIR}/src/app/font_store.cpp
        DEFINITIONS HAS_TTF_FONTS=1 HAS_FILESYSTEM=1 USE_SD_STORAGE=${font_sd} TEST_HEAP_CAPS_ALLOCATOR HOST_LOG_CAPTURE
        INCLUDES ${PROJECT_SOURCE_DIR}/tests/font_store_overrides
        PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/board_config.h ${PROJECT_SOURCE_DIR}/tests/log_manager.h
    )
    set_tests_properties(test_font_store_${font_backend} PROPERTIES TIMEOUT 15)
endforeach()
add_host_gtest(test_gamepad_widgets test_gamepad_widgets.cpp)
target_sources(test_gamepad_widgets PRIVATE ${PROJECT_SOURCE_DIR}/src/app/pad_font_cache.cpp)
add_host_legacy_test(test_font_routes
    SOURCES test_font_routes.cpp ${PROJECT_SOURCE_DIR}/src/app/font_store.cpp ${PROJECT_SOURCE_DIR}/src/app/web_portal_fonts.cpp
        ${PROJECT_SOURCE_DIR}/src/app/button_defaults.cpp ${PROJECT_SOURCE_DIR}/src/app/pad_font_cache.cpp
    DEFINITIONS HAS_TTF_FONTS=1 HAS_FREETYPE_FONTS=0 HAS_FILESYSTEM=1 TEST_HEAP_CAPS_ALLOCATOR
    OPTIONS -Wno-deprecated-declarations
    INCLUDES ${PROJECT_SOURCE_DIR}/tests/font_store_overrides ${HOST_ARDUINOJSON_DIR}
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/board_config.h ${PROJECT_SOURCE_DIR}/tests/font_portal_overrides.h ${PROJECT_SOURCE_DIR}/tests/log_manager.h
    LIBRARIES host_lvgl m
)
target_include_directories(test_gamepad_widgets PRIVATE ${HOST_ARDUINOJSON_DIR})
target_compile_options(test_gamepad_widgets PRIVATE
    -Wno-deprecated-declarations
    "SHELL:-include ${PROJECT_SOURCE_DIR}/tests/Arduino.h"
    "SHELL:-include ${PROJECT_SOURCE_DIR}/tests/board_config.h")
target_link_libraries(test_gamepad_widgets PRIVATE host_lvgl)

add_host_gtest(test_sparkline_widget test_sparkline_widget.cpp)
target_sources(test_sparkline_widget PRIVATE ${PROJECT_SOURCE_DIR}/src/app/pad_font_cache.cpp)
target_include_directories(test_sparkline_widget PRIVATE ${HOST_ARDUINOJSON_DIR})
target_compile_options(test_sparkline_widget PRIVATE
    -Wno-deprecated-declarations -Wno-unused-parameter -Wno-unused-variable -Wno-format-truncation
    "SHELL:-include ${PROJECT_SOURCE_DIR}/tests/Arduino.h"
    "SHELL:-include ${PROJECT_SOURCE_DIR}/tests/board_config.h")
target_link_libraries(test_sparkline_widget PRIVATE host_lvgl m)

add_host_gtest(test_mqtt_subscription_registry test_mqtt_subscription_registry.cpp)

add_host_gtest(test_mqtt_sub_store
    test_mqtt_sub_store.cpp
    ${PROJECT_SOURCE_DIR}/src/app/mqtt_sub_store.cpp)
target_compile_definitions(test_mqtt_sub_store PRIVATE TEST_HEAP_CAPS_ALLOCATOR)
target_compile_options(test_mqtt_sub_store PRIVATE
    -Werror=frame-larger-than=1536
    "SHELL:-include ${PROJECT_SOURCE_DIR}/tests/mqtt_sub_store_test_support.h")

add_host_gtest(test_mqtt_sub_store_headless
    test_mqtt_sub_store.cpp
    ${PROJECT_SOURCE_DIR}/src/app/mqtt_sub_store.cpp)
target_compile_definitions(test_mqtt_sub_store_headless PRIVATE
    TEST_HEAP_CAPS_ALLOCATOR TEST_HAS_DISPLAY=0)
target_compile_options(test_mqtt_sub_store_headless PRIVATE
    -Werror=frame-larger-than=1536
    "SHELL:-include ${PROJECT_SOURCE_DIR}/tests/mqtt_sub_store_test_support.h")

foreach(profile IN ITEMS psram internal)
    add_host_legacy_test(test_mqtt_inspection_${profile}
        SOURCES test_mqtt_inspection.cpp
        INCLUDES ${HOST_ARDUINOJSON_DIR}
        DEFINITIONS TEST_HEAP_CAPS_ALLOCATOR TEST_FREERTOS_MUTEX_LIFECYCLE
        OPTIONS -Wno-deprecated-declarations -Wno-class-memaccess
        LIBRARIES GTest::gtest_main)
    if(profile STREQUAL internal)
        target_compile_definitions(test_mqtt_inspection_${profile} PRIVATE HAS_PSRAM=0)
    endif()
endforeach()

foreach(profile display_only history)
    set(history_enabled 0)
    if(profile STREQUAL history)
        set(history_enabled 1)
    endif()
    add_host_gtest(test_data_stream_${profile}
        test_data_stream_display_only.cpp
        ${PROJECT_SOURCE_DIR}/src/app/data_stream.cpp
        ${PROJECT_SOURCE_DIR}/src/app/ha_stats_resample.cpp
        ${PROJECT_SOURCE_DIR}/src/app/widgets/widget.cpp)
    target_include_directories(test_data_stream_${profile} PRIVATE
        ${HOST_ARDUINOJSON_DIR}
        ${HOST_LVGL_DIR})
    target_compile_definitions(test_data_stream_${profile} PRIVATE
        HAS_DISPLAY=1 HAS_MQTT=0 HAS_HA_LIVE=0 HAS_HA_HISTORY=${history_enabled} HAS_PSRAM=0)
    target_compile_options(test_data_stream_${profile} PRIVATE
        -Wno-deprecated-declarations -Wno-unused-parameter -Wno-unused-variable
        "SHELL:-include ${PROJECT_SOURCE_DIR}/tests/Arduino.h"
        "SHELL:-include ${PROJECT_SOURCE_DIR}/tests/log_manager.h")
    target_link_libraries(test_data_stream_${profile} PRIVATE host_lvgl m)
endforeach()

add_host_gtest(test_fill_draw test_fill_draw.cpp
    ${PROJECT_SOURCE_DIR}/src/app/binding_template.cpp stubs.cpp)
target_include_directories(test_fill_draw BEFORE PRIVATE ${HOST_ARDUINOJSON_DIR})
target_compile_options(test_fill_draw PRIVATE
    "SHELL:-include ${PROJECT_SOURCE_DIR}/tests/log_manager.h")
target_link_libraries(test_fill_draw PRIVATE host_lvgl m)

add_host_legacy_test(test_ota_activity
    SOURCES test_ota_activity.cpp ${PROJECT_SOURCE_DIR}/src/app/ota_activity.cpp
    OPTIONS -pthread
    DEFINITIONS OTA_ACTIVITY_HOST_TEST
)

add_host_legacy_test(test_pad_layout
    SOURCES test_pad_layout.cpp
    INCLUDES ${HOST_LVGL_DIR}
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/Arduino.h ${PROJECT_SOURCE_DIR}/tests/board_config.h
)

add_host_legacy_test(test_stopwatch_round_layout
    SOURCES test_pad_layout.cpp
    DEFINITIONS UI_SCALE_TIER=1
    INCLUDES ${HOST_LVGL_DIR}
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/Arduino.h ${PROJECT_SOURCE_DIR}/tests/board_config.h
)

file(GLOB HOST_PNG_INPUTS CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/assets/png/*.png")
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/png_assets.cpp" "${CMAKE_CURRENT_BINARY_DIR}/png_assets.h"
    COMMAND python3 "${PROJECT_SOURCE_DIR}/tools/png2lvgl_assets.py"
        "${PROJECT_SOURCE_DIR}/assets/png"
        "${CMAKE_CURRENT_BINARY_DIR}/png_assets.cpp"
        "${CMAKE_CURRENT_BINARY_DIR}/png_assets.h"
        --prefix img_
    DEPENDS "${PROJECT_SOURCE_DIR}/tools/png2lvgl_assets.py" ${HOST_PNG_INPUTS}
    VERBATIM
)

add_host_legacy_test(test_stopwatch_round_ui
    SOURCES test_stopwatch_round_ui.cpp ${CMAKE_CURRENT_BINARY_DIR}/png_assets.cpp ${PROJECT_SOURCE_DIR}/src/app/pad_font_cache.cpp ${PROJECT_SOURCE_DIR}/src/app/action_continuation.cpp ${PROJECT_SOURCE_DIR}/src/app/component_registry.cpp
    DEFINITIONS UI_SCALE_TIER=1 DISPLAY_DISABLE_ANIMATIONS=1
        BOOT_APPEARANCE_DEFAULT_FONT_SIZE=0 BOOT_APPEARANCE_DEFAULT_TEXT_RGB=0x646464
        BOOT_APPEARANCE_DEFAULT_BACKGROUND_RGB=0x000000 DISPLAY_SHAPE_ROUND=1 DISPLAY_SHAPE=1
    OPTIONS -Wno-missing-field-initializers -Wno-deprecated-declarations
    INCLUDES ${PROJECT_SOURCE_DIR}/tests/system_screen_overrides
        ${CMAKE_CURRENT_BINARY_DIR} ${HOST_LVGL_DIR} ${HOST_ARDUINOJSON_DIR}
    LIBRARIES host_lvgl
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/Arduino.h ${PROJECT_SOURCE_DIR}/tests/board_config.h
)

add_host_legacy_test(test_numericrocker_zones
    SOURCES test_numericrocker_zones.cpp
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/Arduino.h ${PROJECT_SOURCE_DIR}/tests/board_config.h
)

add_host_legacy_test(test_expr_eval
    SOURCES test_expr_eval.cpp ${PROJECT_SOURCE_DIR}/src/app/expr_eval.cpp
    LIBRARIES m
)

add_host_legacy_test(test_battery_adc_sensor
    SOURCES test_battery_adc_sensor.cpp ${PROJECT_SOURCE_DIR}/src/app/sensors/battery_adc_sensor.cpp
    DEFINITIONS HAS_SENSOR_BATTERY_ADC=0
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/Arduino.h ${PROJECT_SOURCE_DIR}/tests/board_config.h
    LIBRARIES m
)

add_host_legacy_test(test_deferred_dispatch_slot
    SOURCES test_deferred_dispatch_slot.cpp
    OPTIONS -pthread
)

add_host_legacy_test(test_pad_resolve_request_lifetime
    SOURCES test_pad_resolve_request_lifetime.cpp
        ${PROJECT_SOURCE_DIR}/src/app/main_loop_bridge.cpp
        ${PROJECT_SOURCE_DIR}/src/app/pad_resolve_request.cpp
        stubs.cpp
    INCLUDES ${PROJECT_SOURCE_DIR}/tests/pad_resolve_overrides ${HOST_ARDUINOJSON_DIR}
    OPTIONS -Wno-deprecated-declarations
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/pad_resolve_overrides/Arduino.h
        ${PROJECT_SOURCE_DIR}/tests/log_manager.h
        ${PROJECT_SOURCE_DIR}/tests/board_config.h
)

add_host_legacy_test(test_ha_stats_resample
    SOURCES test_ha_stats_resample.cpp ${PROJECT_SOURCE_DIR}/src/app/ha_stats_resample.cpp
    LIBRARIES m
)

add_host_gtest(test_ha_history_decoder
    test_ha_history_decoder.cpp
    ${PROJECT_SOURCE_DIR}/src/app/ha_history_decoder.cpp
    ${PROJECT_SOURCE_DIR}/src/app/ha_stats_resample.cpp)
target_include_directories(test_ha_history_decoder PRIVATE ${HOST_ARDUINOJSON_DIR})
target_compile_options(test_ha_history_decoder PRIVATE -Wno-deprecated-declarations)

add_host_gtest(test_ha_live
    test_ha_live.cpp ${PROJECT_SOURCE_DIR}/src/app/ha_live_state.cpp
    ${PROJECT_SOURCE_DIR}/src/app/ha_lookup_data.cpp)
target_include_directories(test_ha_live PRIVATE ${HOST_ARDUINOJSON_DIR})
target_compile_definitions(test_ha_live PRIVATE HAS_HA_LIVE=1)
target_compile_options(test_ha_live PRIVATE
    "SHELL:-include ${PROJECT_SOURCE_DIR}/tests/board_config.h")

add_host_legacy_test(test_binding_value
    SOURCES test_binding_value.cpp ${PROJECT_SOURCE_DIR}/src/app/binding_value.cpp
    INCLUDES ${HOST_ARDUINOJSON_DIR}
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/Arduino.h
    OPTIONS -Wno-deprecated-declarations)

add_host_legacy_test(test_ha_service_delivery
    SOURCES test_ha_service_delivery.cpp ${PROJECT_SOURCE_DIR}/src/app/ha_service_delivery.cpp
    INCLUDES ${HOST_ARDUINOJSON_DIR}
    DEFINITIONS HAS_MCP=1
    OPTIONS -Wno-deprecated-declarations
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/board_config.h
)

add_host_legacy_test(test_mcp_press_button
    SOURCES test_mcp_press_button.cpp ${PROJECT_SOURCE_DIR}/src/app/mcp_press_button.cpp
    INCLUDES ${HOST_ARDUINOJSON_DIR}
    DEFINITIONS HAS_MCP=1
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/board_config.h
)

add_host_legacy_test(test_expr_binding
    SOURCES test_expr_binding.cpp
        ${PROJECT_SOURCE_DIR}/src/app/binding_template.cpp
        ${PROJECT_SOURCE_DIR}/src/app/expr_eval.cpp
        stubs.cpp
    INCLUDES ${HOST_ARDUINOJSON_DIR}
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/log_manager.h ${PROJECT_SOURCE_DIR}/tests/board_config.h
    LIBRARIES m
)

add_host_legacy_test(test_binding_template
    SOURCES test_binding_template.cpp ${PROJECT_SOURCE_DIR}/src/app/binding_template.cpp stubs.cpp
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/log_manager.h ${PROJECT_SOURCE_DIR}/tests/board_config.h
    LIBRARIES m
)

add_host_legacy_test(test_binding_schema
    SOURCES test_binding_schema.cpp
        ${PROJECT_SOURCE_DIR}/src/app/binding_template.cpp
        ${PROJECT_SOURCE_DIR}/src/app/binding_schema.cpp
        stubs.cpp
    INCLUDES ${HOST_ARDUINOJSON_DIR}
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/log_manager.h ${PROJECT_SOURCE_DIR}/tests/board_config.h
)

add_host_legacy_test(test_voice_auto_stop
    SOURCES test_voice_auto_stop.cpp ${PROJECT_SOURCE_DIR}/src/app/device_classes/voice_assistant/voice_auto_stop.cpp
)

add_host_legacy_test(test_music_catalog
    SOURCES test_music_catalog.cpp
        ${PROJECT_SOURCE_DIR}/src/app/music_catalog.cpp
        ${PROJECT_SOURCE_DIR}/src/app/mp3_metadata.cpp
)

add_host_gtest(test_image_library
    test_image_library.cpp
    ${PROJECT_SOURCE_DIR}/src/app/image_library.cpp
)

add_host_legacy_test(test_music_binding
    SOURCES test_music_binding.cpp
        ${PROJECT_SOURCE_DIR}/src/app/music_binding.cpp
        ${PROJECT_SOURCE_DIR}/src/app/binding_template.cpp
        stubs.cpp
    INCLUDES ${HOST_ARDUINOJSON_DIR}
    DEFINITIONS HAS_AUDIO=1 HAS_SOUND_PLAYER=1 HAS_MCP=1
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/board_config.h ${PROJECT_SOURCE_DIR}/tests/log_manager.h
)

add_host_legacy_test(test_health_table_builder
    SOURCES test_health_table_builder.cpp ${PROJECT_SOURCE_DIR}/src/app/health_table_builder.cpp stubs.cpp
    INCLUDES ${HOST_ARDUINOJSON_DIR}
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/log_manager.h ${PROJECT_SOURCE_DIR}/tests/board_config.h
    LIBRARIES m
)

add_host_legacy_test(test_pad_binding
    SOURCES test_pad_binding.cpp
        ${PROJECT_SOURCE_DIR}/src/app/binding_template.cpp
        ${PROJECT_SOURCE_DIR}/src/app/pad_binding.cpp
        ${PROJECT_SOURCE_DIR}/src/app/expr_eval.cpp
        stubs.cpp
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/log_manager.h ${PROJECT_SOURCE_DIR}/tests/board_config.h
    LIBRARIES m
)

add_host_legacy_test(test_label_style
    SOURCES test_label_style.cpp ${PROJECT_SOURCE_DIR}/src/app/label_style.cpp stubs.cpp
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/log_manager.h ${PROJECT_SOURCE_DIR}/tests/board_config.h
    LIBRARIES m
)

add_host_legacy_test(test_widget_common
    SOURCES test_widget_common.cpp
    LIBRARIES m
)

add_host_legacy_test(test_minimp3_scratch
    SOURCES test_minimp3_scratch.cpp
    LIBRARIES m
)

add_host_legacy_test(test_audio_resampler
    SOURCES test_audio_resampler.cpp
)

add_host_legacy_test(test_tone_alert_overlay
    SOURCES test_tone_alert_overlay.cpp
    LIBRARIES m
)

add_host_legacy_test(test_key_sequence
    SOURCES test_key_sequence.cpp ${PROJECT_SOURCE_DIR}/src/app/key_sequence.cpp
        ${PROJECT_SOURCE_DIR}/src/app/key_sequence_executor.cpp
)
add_host_command_test(check_keyboard_transport_cpp11
    ${CMAKE_CXX_COMPILER} -std=c++11 -pedantic-errors -Wall -Wextra -Werror
    -include ${PROJECT_SOURCE_DIR}/src/app/keyboard_transport.h -x c++ -fsyntax-only /dev/null
)

foreach(transport IN ITEMS both usb ble none)
    if(transport STREQUAL "usb" OR transport STREQUAL "both")
        set(test_usb_transport 1)
    else()
        set(test_usb_transport 0)
    endif()
    if(transport STREQUAL "ble" OR transport STREQUAL "both")
        set(test_ble_transport 1)
    else()
        set(test_ble_transport 0)
    endif()
    add_host_legacy_test(test_key_action_${transport}
        SOURCES test_key_action_transport.cpp ${PROJECT_SOURCE_DIR}/src/app/key_sequence.cpp
            ${PROJECT_SOURCE_DIR}/src/app/key_sequence_executor.cpp
        INCLUDES ${HOST_ARDUINOJSON_DIR}
        DEFINITIONS ARDUINO=1 TEST_USB_TRANSPORT=${test_usb_transport} TEST_BLE_TRANSPORT=${test_ble_transport}
            ARDUINOJSON_ENABLE_PROGMEM=0 ARDUINOJSON_ENABLE_ARDUINO_STRING=0
            ARDUINOJSON_ENABLE_ARDUINO_STREAM=0 ARDUINOJSON_ENABLE_ARDUINO_PRINT=0
        PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/log_manager.h ${PROJECT_SOURCE_DIR}/tests/board_config.h
        OPTIONS -Wno-deprecated-declarations
    )
endforeach()

set(ACTION_TEST_SOURCES
    ${PROJECT_SOURCE_DIR}/src/app/actions.cpp ${PROJECT_SOURCE_DIR}/src/app/music_command.cpp
    ${PROJECT_SOURCE_DIR}/src/app/action_parse.cpp ${PROJECT_SOURCE_DIR}/src/app/action_list.cpp
    ${PROJECT_SOURCE_DIR}/src/app/action_continuation.cpp ${PROJECT_SOURCE_DIR}/src/app/pad_cycle.cpp
    ${PROJECT_SOURCE_DIR}/src/app/action_registry.cpp ${PROJECT_SOURCE_DIR}/src/app/binding_template.cpp stubs.cpp
)

add_host_legacy_test(test_action_parse
    SOURCES test_action_parse.cpp ${ACTION_TEST_SOURCES} ${PROJECT_SOURCE_DIR}/src/app/action_catalog.cpp
    INCLUDES ${HOST_ARDUINOJSON_DIR}
    DEFINITIONS ALARM_ENABLED=1 HAS_AUDIO=1 HAS_SOUND_PLAYER=1 HAS_TOUCH=1
    OPTIONS -Wno-deprecated-declarations
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/log_manager.h ${PROJECT_SOURCE_DIR}/tests/board_config.h
)
add_host_legacy_test(test_action_parse_alarm_disabled
    SOURCES test_action_parse.cpp ${ACTION_TEST_SOURCES} ${PROJECT_SOURCE_DIR}/src/app/action_catalog.cpp
        ${PROJECT_SOURCE_DIR}/src/app/alarm_manager.cpp
    INCLUDES ${HOST_ARDUINOJSON_DIR}
    DEFINITIONS ALARM_ENABLED=0 HAS_AUDIO=1 HAS_SOUND_PLAYER=1 HAS_TOUCH=1
    OPTIONS -Wno-deprecated-declarations
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/log_manager.h ${PROJECT_SOURCE_DIR}/tests/board_config.h)
add_host_legacy_test(test_action_bindings
    SOURCES test_action_bindings.cpp ${ACTION_TEST_SOURCES}
    INCLUDES ${HOST_ARDUINOJSON_DIR}
    DEFINITIONS HAS_AUDIO=1 HAS_SOUND_PLAYER=1
    OPTIONS -Wno-deprecated-declarations
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/log_manager.h ${PROJECT_SOURCE_DIR}/tests/board_config.h
    LIBRARIES m
)
add_host_legacy_test(test_action_continuation
    SOURCES test_action_continuation.cpp ${PROJECT_SOURCE_DIR}/src/app/action_list.cpp ${PROJECT_SOURCE_DIR}/src/app/action_continuation.cpp stubs.cpp
    INCLUDES ${HOST_ARDUINOJSON_DIR}
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/log_manager.h ${PROJECT_SOURCE_DIR}/tests/board_config.h
)
add_host_legacy_test(test_action_registry_value_field
    SOURCES test_action_registry_value_field.cpp ${PROJECT_SOURCE_DIR}/src/app/action_registry.cpp ${PROJECT_SOURCE_DIR}/src/app/binding_template.cpp stubs.cpp
    INCLUDES ${HOST_ARDUINOJSON_DIR}
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/log_manager.h ${PROJECT_SOURCE_DIR}/tests/board_config.h
    LIBRARIES m
)
add_host_legacy_test(test_action_sizes
    SOURCES test_action_sizes.cpp stubs.cpp
    INCLUDES ${HOST_ARDUINOJSON_DIR}
    OPTIONS -Wno-unused
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/log_manager.h ${PROJECT_SOURCE_DIR}/tests/board_config.h
)
add_host_legacy_test(test_pad_confirmation
    SOURCES test_pad_confirmation.cpp ${PROJECT_SOURCE_DIR}/src/app/pad_validate.cpp ${PROJECT_SOURCE_DIR}/src/app/action_validate.cpp stubs.cpp
    INCLUDES ${HOST_ARDUINOJSON_DIR}
    OPTIONS -Wno-deprecated-declarations
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/log_manager.h ${PROJECT_SOURCE_DIR}/tests/board_config.h
)
add_host_legacy_test(test_camera_capture_action
    SOURCES test_camera_capture_action.cpp ${PROJECT_SOURCE_DIR}/src/app/action_continuation.cpp ${PROJECT_SOURCE_DIR}/src/app/action_registry.cpp ${PROJECT_SOURCE_DIR}/src/app/binding_template.cpp stubs.cpp
    INCLUDES ${HOST_ARDUINOJSON_DIR}
    DEFINITIONS HAS_CAMERA=1
    OPTIONS -Wno-deprecated-declarations
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/log_manager.h ${PROJECT_SOURCE_DIR}/tests/board_config.h
)
add_host_legacy_test(test_pad_cycle SOURCES test_pad_cycle.cpp ${PROJECT_SOURCE_DIR}/src/app/pad_cycle.cpp PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/board_config.h)
add_host_legacy_test(test_pad_config_dynamic_layout SOURCES test_pad_config_dynamic_layout.cpp PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/Arduino.h ${PROJECT_SOURCE_DIR}/tests/board_config.h)
add_host_legacy_test(test_parse_hex_color SOURCES test_parse_hex_color.cpp PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/Arduino.h ${PROJECT_SOURCE_DIR}/tests/board_config.h)
add_host_legacy_test(test_wifi_reconnect SOURCES test_wifi_reconnect.cpp ${PROJECT_SOURCE_DIR}/src/app/wifi_reconnect.cpp)
add_host_legacy_test(test_screen_saver_schedule SOURCES test_screen_saver_schedule.cpp)
add_host_legacy_test(test_component_registry SOURCES test_component_registry.cpp ${PROJECT_SOURCE_DIR}/src/app/component_registry.cpp stubs.cpp PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/log_manager.h ${PROJECT_SOURCE_DIR}/tests/board_config.h)
add_host_command_test(test_portal_asset_cache bash ${PROJECT_SOURCE_DIR}/tests/test_portal_asset_cache.sh)

add_host_legacy_test(test_timer_binding_target
    SOURCES test_timer_binding_target.cpp ${PROJECT_SOURCE_DIR}/src/app/timer_binding.cpp ${PROJECT_SOURCE_DIR}/src/app/timer_engine.cpp ${PROJECT_SOURCE_DIR}/src/app/config_psram.cpp ${PROJECT_SOURCE_DIR}/src/app/binding_template.cpp stubs.cpp
    INCLUDES ${PROJECT_SOURCE_DIR}/tests/timer_test_overrides ${HOST_ARDUINOJSON_DIR} ${HOST_LVGL_DIR}
    DEFINITIONS DEFAULT_PIXEL_SHIFT_DISTANCE=4
    OPTIONS -ffunction-sections -fdata-sections -Wno-deprecated-declarations
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/timer_test_overrides/board_config.h ${PROJECT_SOURCE_DIR}/tests/log_manager.h
    LIBRARIES m pthread -Wl,--gc-sections
)
add_host_legacy_test(test_timer_commands
    SOURCES test_timer_commands.cpp ${PROJECT_SOURCE_DIR}/src/app/timer_command.cpp ${PROJECT_SOURCE_DIR}/src/app/timer_mcp_adapter.cpp ${PROJECT_SOURCE_DIR}/src/app/timer_engine.cpp ${PROJECT_SOURCE_DIR}/src/app/config_psram.cpp stubs.cpp
    INCLUDES ${PROJECT_SOURCE_DIR}/tests/timer_test_overrides ${HOST_ARDUINOJSON_DIR}
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/timer_test_overrides/board_config.h ${PROJECT_SOURCE_DIR}/tests/log_manager.h
    LIBRARIES m pthread -Wl,--wrap=calloc
)
add_test(NAME test_timer_commands_pre_init_controls COMMAND test_timer_commands pre-init-controls)
add_test(NAME test_timer_commands_mutex_failure COMMAND test_timer_commands mutex-failure)
add_test(NAME test_timer_commands_allocation_failure COMMAND test_timer_commands allocation-failure)
add_host_legacy_test(test_timer_config
    SOURCES test_timer_config.cpp ${PROJECT_SOURCE_DIR}/src/app/timer_config.cpp ${PROJECT_SOURCE_DIR}/src/app/config_psram.cpp
        ${PROJECT_SOURCE_DIR}/src/app/actions.cpp ${PROJECT_SOURCE_DIR}/src/app/music_command.cpp
        ${PROJECT_SOURCE_DIR}/src/app/action_parse.cpp ${PROJECT_SOURCE_DIR}/src/app/action_continuation.cpp
        ${PROJECT_SOURCE_DIR}/src/app/pad_cycle.cpp ${PROJECT_SOURCE_DIR}/src/app/action_registry.cpp stubs.cpp
    INCLUDES ${PROJECT_SOURCE_DIR}/tests/timer_test_overrides ${HOST_ARDUINOJSON_DIR}
    DEFINITIONS HAS_FILESYSTEM=1
    OPTIONS -Wno-deprecated-declarations
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/timer_test_overrides/board_config.h ${PROJECT_SOURCE_DIR}/tests/log_manager.h ${PROJECT_SOURCE_DIR}/tests/Arduino.h
    LIBRARIES m pthread
)
add_test(NAME test_timer_config_config_mutex_failure COMMAND test_timer_config config-mutex-failure)
add_test(NAME test_timer_config_save_mutex_failure COMMAND test_timer_config save-mutex-failure)
add_host_legacy_test(test_alarm_manager
    SOURCES test_alarm_manager.cpp ${PROJECT_SOURCE_DIR}/src/app/alarm_manager.cpp
        ${PROJECT_SOURCE_DIR}/src/app/config_psram.cpp
        ${PROJECT_SOURCE_DIR}/src/app/actions.cpp ${PROJECT_SOURCE_DIR}/src/app/music_command.cpp
        ${PROJECT_SOURCE_DIR}/src/app/action_parse.cpp ${PROJECT_SOURCE_DIR}/src/app/action_validate.cpp
        ${PROJECT_SOURCE_DIR}/src/app/action_continuation.cpp ${PROJECT_SOURCE_DIR}/src/app/binding_template.cpp
        ${PROJECT_SOURCE_DIR}/src/app/pad_cycle.cpp ${PROJECT_SOURCE_DIR}/src/app/action_registry.cpp stubs.cpp
    INCLUDES ${PROJECT_SOURCE_DIR}/tests/timer_test_overrides ${HOST_ARDUINOJSON_DIR}
    DEFINITIONS ALARM_ENABLED=1 HOST_LOG_CAPTURE=1 HAS_FILESYSTEM=1
    OPTIONS -Wno-deprecated-declarations
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/timer_test_overrides/board_config.h ${PROJECT_SOURCE_DIR}/tests/log_manager.h ${PROJECT_SOURCE_DIR}/tests/Arduino.h
    LIBRARIES m pthread -Wl,--wrap=calloc)
add_test(NAME test_alarm_manager_allocation_failure COMMAND test_alarm_manager allocation-failure)
add_host_command_test(test_static_state python3 ${PROJECT_SOURCE_DIR}/tests/test_static_state.py)
add_host_legacy_test(test_timer_format SOURCES test_timer_format.cpp ${PROJECT_SOURCE_DIR}/src/app/config_psram.cpp stubs.cpp INCLUDES ${HOST_ARDUINOJSON_DIR} PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/board_config.h ${PROJECT_SOURCE_DIR}/tests/log_manager.h)
add_host_legacy_test(test_sound_player_loop
    SOURCES test_sound_player_loop.cpp
    INCLUDES ${PROJECT_SOURCE_DIR}/tests/timer_test_overrides
    DEFINITIONS HAS_AUDIO=1 HAS_SOUND_PLAYER=1 HAS_FILESYSTEM=1 AUDIO_SAMPLE_RATE=48000
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/board_config.h ${PROJECT_SOURCE_DIR}/tests/log_manager.h
    LIBRARIES m)

foreach(audio_profile IN ITEMS disabled beep mp3)
    set(profile_audio 1)
    set(profile_mp3 0)
    if(audio_profile STREQUAL "disabled")
        set(profile_audio 0)
    elseif(audio_profile STREQUAL "mp3")
        set(profile_mp3 1)
    endif()
    add_host_legacy_test(test_audio_worker_${audio_profile}
        SOURCES test_audio_worker.cpp ${PROJECT_SOURCE_DIR}/src/app/music_transport.cpp ${PROJECT_SOURCE_DIR}/src/app/music_catalog.cpp
        INCLUDES ${PROJECT_SOURCE_DIR}/tests/audio_worker_overrides ${HOST_ARDUINOJSON_DIR}
        DEFINITIONS HAS_AUDIO=${profile_audio} HAS_SOUND_PLAYER=${profile_mp3} HAS_MUSIC_ANALYSIS=0 AUDIO_SAMPLE_RATE=48000 AUDIO_DMA_DESC_NUM=6 AUDIO_DMA_FRAME_NUM=512 AUDIO_TASK_STACK_SIZE=16384
        PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/board_config.h ${PROJECT_SOURCE_DIR}/tests/log_manager.h ${PROJECT_SOURCE_DIR}/tests/Arduino.h
        LIBRARIES m)
endforeach()

add_host_legacy_test(test_shutter_curtain_stats SOURCES test_shutter_curtain_stats.cpp ${PROJECT_SOURCE_DIR}/src/app/device_classes/shutter_tester/shutter_curtain_stats.cpp INCLUDES ${PROJECT_SOURCE_DIR}/src/app/device_classes/shutter_tester LIBRARIES m)
add_host_legacy_test(test_shutter_capture SOURCES test_shutter_capture.cpp INCLUDES ${PROJECT_SOURCE_DIR}/src/app/device_classes/shutter_tester DEFINITIONS IS_SHUTTER_TESTER=true PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/log_manager.h)
add_host_legacy_test(test_shutter_measure SOURCES test_shutter_measure.cpp stubs.cpp INCLUDES ${PROJECT_SOURCE_DIR}/src/app/device_classes/shutter_tester DEFINITIONS IS_SHUTTER_TESTER=true PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/log_manager.h LIBRARIES m)
add_host_legacy_test(test_shutter_measure_no_csv SOURCES test_shutter_measure.cpp stubs.cpp INCLUDES ${PROJECT_SOURCE_DIR}/src/app/device_classes/shutter_tester DEFINITIONS IS_SHUTTER_TESTER=true LOG_SHUTTER_CSV=0 PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/log_manager.h LIBRARIES m)
add_host_legacy_test(test_shutter_binding SOURCES test_shutter_binding.cpp stubs.cpp INCLUDES ${PROJECT_SOURCE_DIR}/src/app/device_classes/shutter_tester DEFINITIONS HAS_DISPLAY=true IS_SHUTTER_TESTER=true PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/log_manager.h LIBRARIES m)
add_host_legacy_test(test_shutter_session SOURCES test_shutter_session.cpp ${PROJECT_SOURCE_DIR}/src/app/device_classes/shutter_tester/shutter_session.cpp ${PROJECT_SOURCE_DIR}/src/app/device_classes/shutter_tester/shutter_curtain_stats.cpp stubs.cpp INCLUDES ${PROJECT_SOURCE_DIR}/tests/shutter_session_overrides ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/src/app/device_classes/shutter_tester ${HOST_ARDUINOJSON_DIR} DEFINITIONS IS_SHUTTER_TESTER=true HAS_FILESYSTEM=1 OPTIONS -Wno-deprecated-declarations PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/log_manager.h LIBRARIES m)
add_host_legacy_test(test_shutter_session_actions
    SOURCES test_shutter_session_actions.cpp ${PROJECT_SOURCE_DIR}/src/app/actions.cpp
        ${PROJECT_SOURCE_DIR}/src/app/music_command.cpp ${PROJECT_SOURCE_DIR}/src/app/action_parse.cpp
        ${PROJECT_SOURCE_DIR}/src/app/action_continuation.cpp ${PROJECT_SOURCE_DIR}/src/app/pad_cycle.cpp
        ${PROJECT_SOURCE_DIR}/src/app/action_registry.cpp ${PROJECT_SOURCE_DIR}/src/app/binding_template.cpp stubs.cpp
    INCLUDES ${PROJECT_SOURCE_DIR}/src/app/device_classes/shutter_tester ${HOST_ARDUINOJSON_DIR}
    OPTIONS -Wno-deprecated-declarations
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/log_manager.h ${PROJECT_SOURCE_DIR}/tests/board_config.h
)

add_host_legacy_test(test_list_provider SOURCES test_list_provider.cpp ${PROJECT_SOURCE_DIR}/src/app/list_provider.cpp ${PROJECT_SOURCE_DIR}/src/app/list_binding.cpp ${PROJECT_SOURCE_DIR}/src/app/binding_template.cpp stubs.cpp PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/log_manager.h ${PROJECT_SOURCE_DIR}/tests/board_config.h)
add_host_legacy_test(test_net_activity SOURCES test_net_activity.cpp ${PROJECT_SOURCE_DIR}/src/app/net_activity.cpp)
add_host_legacy_test(test_epaper_frame_battery SOURCES test_epaper_frame_battery.cpp)
add_host_legacy_test(test_epaper_frame_media_conformance SOURCES test_epaper_frame_media_conformance.cpp ${PROJECT_SOURCE_DIR}/src/app/device_classes/epaper_frame/epaper_frame_media_validation.cpp ${PROJECT_SOURCE_DIR}/src/app/device_classes/epaper_frame/epaper_frame_transport_crc32.cpp LIBRARIES z)
add_host_legacy_test(test_coffee_scale_cmd_len SOURCES test_coffee_scale_cmd_len.cpp DEFINITIONS IS_COFFEE_SCALE=true PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/board_config.h)
add_host_legacy_test(test_expose_dry_down SOURCES test_expose_dry_down.cpp INCLUDES ${PROJECT_SOURCE_DIR}/src/app/device_classes/darkroom_timer DEFINITIONS IS_DARKROOM_TIMER=1 HAS_AUDIO=0 LIBRARIES m)
add_host_legacy_test(test_meter_mag SOURCES test_meter_mag.cpp INCLUDES ${PROJECT_SOURCE_DIR}/src/app/device_classes/darkroom_timer DEFINITIONS IS_DARKROOM_TIMER=1 LIBRARIES m)
add_host_legacy_test(test_print_log SOURCES test_print_log.cpp ${PROJECT_SOURCE_DIR}/src/app/time_service.cpp INCLUDES ${PROJECT_SOURCE_DIR}/tests/print_log_overrides ${PROJECT_SOURCE_DIR}/src/app/device_classes/darkroom_timer ${HOST_ARDUINOJSON_DIR} DEFINITIONS IS_DARKROOM_TIMER=1 HAS_FILESYSTEM=1 LIBRARIES m pthread)
add_host_legacy_test(test_mcp_result_strings SOURCES test_mcp_result_strings.cpp INCLUDES ${HOST_ARDUINOJSON_DIR})
add_host_legacy_test(test_mcp_device_identity SOURCES test_mcp_device_identity.cpp ${PROJECT_SOURCE_DIR}/src/app/mcp_device_identity.cpp INCLUDES ${HOST_ARDUINOJSON_DIR} DEFINITIONS HAS_MCP=1 PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/Arduino.h ${PROJECT_SOURCE_DIR}/tests/board_config.h)
add_host_legacy_test(test_fs_health_littlefs SOURCES test_fs_health.cpp ${PROJECT_SOURCE_DIR}/src/app/fs_health.cpp DEFINITIONS USE_SD_STORAGE=0 PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/board_config.h)
add_host_legacy_test(test_fs_health_sdmmc SOURCES test_fs_health.cpp ${PROJECT_SOURCE_DIR}/src/app/fs_health.cpp DEFINITIONS USE_SD_STORAGE=1 HAS_SD_CARD=1 PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/board_config.h)
add_host_legacy_test(test_fs_health_telemetry SOURCES test_fs_health_telemetry.cpp INCLUDES ${HOST_ARDUINOJSON_DIR} PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/Arduino.h ${PROJECT_SOURCE_DIR}/tests/board_config.h)
add_host_legacy_test(test_storage_policy_littlefs SOURCES test_storage_policy.cpp INCLUDES ${PROJECT_SOURCE_DIR}/tests/storage_policy_overrides DEFINITIONS USE_SD_STORAGE=0)
add_host_legacy_test(test_storage_policy_sdmmc SOURCES test_storage_policy.cpp INCLUDES ${PROJECT_SOURCE_DIR}/tests/storage_policy_overrides DEFINITIONS USE_SD_STORAGE=1 HAS_SD_CARD=1)
add_host_legacy_test(test_factory_reset_storage SOURCES test_factory_reset_storage.cpp INCLUDES ${PROJECT_SOURCE_DIR}/tests/factory_reset_overrides OPTIONS -ffunction-sections -fdata-sections LIBRARIES -Wl,--gc-sections)
add_host_legacy_test(test_factory_reset_no_filesystem SOURCES test_factory_reset_storage.cpp
    INCLUDES ${PROJECT_SOURCE_DIR}/tests/factory_reset_overrides
    DEFINITIONS HAS_FILESYSTEM=0 USE_SD_STORAGE=0
    OPTIONS -ffunction-sections -fdata-sections LIBRARIES -Wl,--gc-sections)

# Real ArduinoJson for the schema emitter; the profiles use a minimal JSON stub.
add_library(binding_schema_json OBJECT
    ${PROJECT_SOURCE_DIR}/src/app/binding_schema.cpp binding_schema_profiles/dump_json.cpp)
target_compile_features(binding_schema_json PRIVATE cxx_std_17)
target_compile_options(binding_schema_json PRIVATE -Wall -Wextra -Werror)
target_include_directories(binding_schema_json PRIVATE ${HOST_ARDUINOJSON_DIR} ${PROJECT_SOURCE_DIR}/src/app)

add_library(binding_schema_values STATIC ${PROJECT_SOURCE_DIR}/src/app/binding_value.cpp)
target_include_directories(binding_schema_values PRIVATE ${HOST_ARDUINOJSON_DIR}
    ${PROJECT_SOURCE_DIR}/tests ${PROJECT_SOURCE_DIR}/src/app)
target_compile_options(binding_schema_values PRIVATE -Wno-deprecated-declarations)
target_link_libraries(test_pad_binding PRIVATE binding_schema_values)

function(add_binding_schema_profile name definition)
    add_host_legacy_test(test_binding_schema_${name}
        SOURCES test_binding_schema_production.cpp binding_schema_profiles/stubs.cpp
            ${PROJECT_SOURCE_DIR}/src/app/binding_template.cpp ${PROJECT_SOURCE_DIR}/src/app/binding_builtin_schemes.cpp
            ${PROJECT_SOURCE_DIR}/src/app/health_binding.cpp ${PROJECT_SOURCE_DIR}/src/app/timer_binding.cpp
            ${PROJECT_SOURCE_DIR}/src/app/sensor_binding.cpp
            ${PROJECT_SOURCE_DIR}/src/app/alarm_binding.cpp
            ${PROJECT_SOURCE_DIR}/src/app/music_binding.cpp ${PROJECT_SOURCE_DIR}/src/app/net_binding.cpp
            ${PROJECT_SOURCE_DIR}/src/app/list_binding.cpp ${PROJECT_SOURCE_DIR}/src/app/list_provider.cpp
            ${PROJECT_SOURCE_DIR}/src/app/expr_binding.cpp ${PROJECT_SOURCE_DIR}/src/app/pad_binding.cpp
            ${PROJECT_SOURCE_DIR}/src/app/camera_binding.cpp ${PROJECT_SOURCE_DIR}/src/app/expr_eval.cpp
            ${PROJECT_SOURCE_DIR}/src/app/time_binding.cpp ${PROJECT_SOURCE_DIR}/src/app/time_service.cpp
            ${PROJECT_SOURCE_DIR}/src/app/mqtt_binding.cpp ${PROJECT_SOURCE_DIR}/src/app/config_psram.cpp
            ${PROJECT_SOURCE_DIR}/src/app/ha_binding.cpp
            $<TARGET_OBJECTS:binding_schema_json> ${ARGN}
        INCLUDES ${PROJECT_SOURCE_DIR}/tests/binding_schema_profiles ${PROJECT_SOURCE_DIR}/src
        DEFINITIONS ${definition}
        OPTIONS -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function
        LIBRARIES binding_schema_values
        PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/Arduino.h ${PROJECT_SOURCE_DIR}/tests/binding_schema_profiles/platform_stubs.h
    )
endfunction()

add_binding_schema_profile(baseline "")
add_binding_schema_profile(display_only "HAS_MQTT=0;HAS_HA_LIVE=0")
add_binding_schema_profile(alarm_disabled "ALARM_ENABLED=0")
add_binding_schema_profile(camera BINDING_SCHEMA_PROFILE_CAMERA)
add_binding_schema_profile(full BINDING_SCHEMA_PROFILE_FULL ${PROJECT_SOURCE_DIR}/src/app/audio_input_binding.cpp ${PROJECT_SOURCE_DIR}/src/app/image_binding.cpp)
# Portal dev-server fixture for GET /api/bindings?docs=1; regenerate with
# build/host-tests/tests/test_binding_schema_full --emit-json > tools/mock-data/binding-docs.json
add_test(NAME test_binding_docs_fixture
    COMMAND sh -c "$<TARGET_FILE:test_binding_schema_full> --emit-json | diff -u tools/mock-data/binding-docs.json -")
set_tests_properties(test_binding_docs_fixture PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
add_binding_schema_profile(full_no_psram "BINDING_SCHEMA_PROFILE_FULL;HAS_PSRAM=0" ${PROJECT_SOURCE_DIR}/src/app/audio_input_binding.cpp ${PROJECT_SOURCE_DIR}/src/app/image_binding.cpp)
add_binding_schema_profile(ha_only "HAS_HA_LIVE=1;HAS_MQTT=0")
add_binding_schema_profile(voice BINDING_SCHEMA_PROFILE_VOICE ${PROJECT_SOURCE_DIR}/src/app/device_classes/voice_assistant/voice_binding.cpp)
add_binding_schema_profile(coffee BINDING_SCHEMA_PROFILE_COFFEE ${PROJECT_SOURCE_DIR}/src/app/device_classes/coffee_scale/scale_binding.cpp ${PROJECT_SOURCE_DIR}/src/app/device_classes/coffee_scale/brew/brew_binding.cpp)
add_binding_schema_profile(darkroom "BINDING_SCHEMA_PROFILE_DARKROOM;HAS_FILESYSTEM=1" ${PROJECT_SOURCE_DIR}/src/app/device_classes/darkroom_timer/print_log.cpp ${PROJECT_SOURCE_DIR}/src/app/device_classes/darkroom_timer/meter.cpp ${PROJECT_SOURCE_DIR}/src/app/device_classes/darkroom_timer/expose_timer.cpp ${PROJECT_SOURCE_DIR}/src/app/device_classes/darkroom_timer/test_strip.cpp)
add_binding_schema_profile(shutter BINDING_SCHEMA_PROFILE_SHUTTER ${PROJECT_SOURCE_DIR}/src/app/device_classes/shutter_tester/shutter_binding.cpp)

function(add_host_compile_check target source)
    add_library(${target} OBJECT ${source})
    target_compile_features(${target} PRIVATE cxx_std_17)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror ${ARGN})
    target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/tests ${PROJECT_SOURCE_DIR}/src/app ${HOST_ARDUINOJSON_DIR} ${HOST_LVGL_DIR})
    target_compile_definitions(${target} PRIVATE HAS_HA_HISTORY=0 HAS_MCP=0)
    target_compile_options(${target} PRIVATE "SHELL:-include ${PROJECT_SOURCE_DIR}/tests/Arduino.h" "SHELL:-include ${PROJECT_SOURCE_DIR}/tests/log_manager.h" "SHELL:-include ${PROJECT_SOURCE_DIR}/tests/board_config.h")
    add_test(NAME ${target} COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ${target})
endfunction()

add_host_compile_check(check_sparkline_no_ha ${PROJECT_SOURCE_DIR}/src/app/widgets/sparkline_widget.cpp -Wno-unused-parameter -Wno-unused-variable -Wno-unused-but-set-variable -Wno-format-truncation)
add_host_compile_check(check_ha_service_delivery_no_mcp ${PROJECT_SOURCE_DIR}/src/app/ha_service_delivery.cpp)
add_host_compile_check(check_mcp_press_button_no_mcp ${PROJECT_SOURCE_DIR}/src/app/mcp_press_button.cpp)
add_host_compile_check(check_mcp_device_identity_no_mcp ${PROJECT_SOURCE_DIR}/src/app/mcp_device_identity.cpp)

foreach(script
    test_brew_storage_and_tare_guards.sh test_branding_mirror.sh test_asset_variant_matrix.sh test_word_clock_logic.sh
    test_extension_config_contracts.sh
    test_camera_preview_widget.sh test_camera_motion.sh test_mcp_scheme_parity.sh
    test_action_catalog_completeness.sh test_mcp_storage_parity.sh test_display_safe_telemetry.sh
    test_rtos_task_stack_helpers.sh
    test_sd_wifi_order.sh test_photoframe_vector_drift.sh test_epaper_frame_portal_contract.sh
    test_epaper_macro_contract.sh test_esp_web_tools_site.sh
)
    get_filename_component(name ${script} NAME_WE)
    add_host_command_test(${name} bash ${PROJECT_SOURCE_DIR}/tests/${script})
endforeach()

add_host_command_test(test_portal_component_capacity_matrix python3 ${PROJECT_SOURCE_DIR}/tests/test_portal_component_capacity_matrix.py)
add_host_command_test(test_widget_schema python3 ${PROJECT_SOURCE_DIR}/tools/lint_widget_schema.py)
add_host_command_test(test_gauge_caption_cache python3 ${PROJECT_SOURCE_DIR}/tests/test_gauge_caption_cache.py)
add_host_legacy_test(test_display_perf SOURCES test_display_perf.cpp DEFINITIONS HAS_DISPLAY_DIAGNOSTICS=0)
add_host_legacy_test(test_display_perf_diagnostics SOURCES test_display_perf.cpp DEFINITIONS HAS_DISPLAY_DIAGNOSTICS=1)
add_host_command_test(test_pad_refresh_schedule python3 ${PROJECT_SOURCE_DIR}/tests/test_pad_refresh_schedule.py)
add_host_command_test(test_portal_info_preparation python3 ${PROJECT_SOURCE_DIR}/tests/test_portal_info_preparation.py)
add_host_command_test(test_portal_dev_server python3 ${PROJECT_SOURCE_DIR}/tests/test_portal_dev_server.py)
add_host_command_test(test_gt911_touch python3 ${PROJECT_SOURCE_DIR}/tests/test_gt911_touch.py)
add_host_command_test(test_touch_driver_samples python3 ${PROJECT_SOURCE_DIR}/tests/test_touch_driver_samples.py)
add_host_command_test(test_touch_manager_snapshots python3 ${PROJECT_SOURCE_DIR}/tests/test_touch_manager_snapshots.py)
add_host_command_test(test_usb_hid_naming python3 ${PROJECT_SOURCE_DIR}/tests/test_usb_hid_naming.py)
add_host_command_test(test_display_perf_api python3 ${PROJECT_SOURCE_DIR}/tests/test_display_perf_api.py)
add_host_command_test(test_timer_mcp_integration python3 ${PROJECT_SOURCE_DIR}/tests/test_timer_mcp_integration.py)

foreach(script
    test_timer_action_editor.js test_portal_action_editor_cycle.js test_portal_action_picker.js
    test_portal_action_list.js test_portal_timer_binding.js test_portal_pad_dialog_transaction.js
    test_portal_pad_import.js test_portal_pad_dirty.js test_portal_recipes.js
    test_portal_ble_config.js test_portal_audio_config.js test_portal_epaper_config.js test_portal_pad_preview.js test_portal_startup.js
    test_portal_logs.js test_portal_screensaver.js test_portal_pad_colors.js
    test_portal_binding_help.js test_portal_asset_gates.js
    test_portal_extension_upload.js
)
    get_filename_component(name ${script} NAME_WE)
    add_host_command_test(${name} node ${PROJECT_SOURCE_DIR}/tests/${script})
    if(script STREQUAL "test_portal_action_picker.js")
        set_tests_properties(${name} PROPERTIES ENVIRONMENT "ACTION_CATALOG_BINARY=$<TARGET_FILE:test_action_parse>")
    endif()
endforeach()

file(GLOB action_editor_scripts CONFIGURE_DEPENDS RELATIVE "${PROJECT_SOURCE_DIR}/src/app/web"
    "${PROJECT_SOURCE_DIR}/src/app/web/portal_action_editor*.js")
foreach(script ${action_editor_scripts} portal_binding_validator.js portal_pad_dialog.js portal_config_actions.js portal_recipes.js)
    get_filename_component(name ${script} NAME_WE)
    add_host_command_test(check_${name}_syntax node --check ${PROJECT_SOURCE_DIR}/src/app/web/${script})
endforeach()

add_host_gtest(test_epaper_frame_carousel
    test_epaper_frame_carousel.cpp
    ${PROJECT_SOURCE_DIR}/src/app/device_classes/epaper_frame/epaper_frame_carousel.cpp
)
target_compile_definitions(test_epaper_frame_carousel PRIVATE IS_EPAPER_FRAME=1)

add_host_gtest(test_epaper_frame_schedule
    test_epaper_frame_schedule.cpp
    ${PROJECT_SOURCE_DIR}/src/app/device_classes/epaper_frame/epaper_frame_schedule.cpp
)
target_compile_definitions(test_epaper_frame_schedule PRIVATE IS_EPAPER_FRAME=1)

add_host_gtest(test_button_shadow_color
    test_button_shadow_color.cpp
)

add_host_gtest(test_audio_gain
    test_audio_gain.cpp
)

add_host_gtest(test_image_rgba_source
    test_image_rgba_source.cpp
)

add_host_gtest(test_music_command
    test_music_command.cpp
    ${PROJECT_SOURCE_DIR}/src/app/music_command.cpp
)

add_host_gtest(test_music_transport
    test_music_transport.cpp
    ${PROJECT_SOURCE_DIR}/src/app/music_transport.cpp
)

add_host_gtest(test_voice_transcription_request
    test_voice_transcription_request.cpp
    ${PROJECT_SOURCE_DIR}/src/app/device_classes/voice_assistant/voice_transcription_request.cpp
)

add_host_gtest(test_voice_wav
    test_voice_wav.cpp
    ${PROJECT_SOURCE_DIR}/src/app/device_classes/voice_assistant/voice_wav.cpp
)

add_host_gtest(test_voice_tts_request
    test_voice_tts_request.cpp
    ${PROJECT_SOURCE_DIR}/src/app/device_classes/voice_assistant/voice_tts_request.cpp
)

add_host_gtest(test_epaper_frame_next_client_logic
    test_epaper_frame_next_client_logic.cpp
    ${PROJECT_SOURCE_DIR}/src/app/device_classes/epaper_frame/epaper_frame_next_client_logic.cpp
    ${PROJECT_SOURCE_DIR}/src/app/device_classes/epaper_frame/epaper_frame_transport_crc32.cpp
)

add_host_gtest(test_epaper_frame_offline_queue
    test_epaper_frame_offline_queue.cpp
    ${PROJECT_SOURCE_DIR}/src/app/device_classes/epaper_frame/epaper_frame_offline_queue_logic.cpp
)

add_host_gtest(test_power_config
    test_power_config.cpp
    ${PROJECT_SOURCE_DIR}/src/app/power_config.cpp
)

add_host_gtest(test_power_manager
    test_power_manager.cpp
    ${PROJECT_SOURCE_DIR}/src/app/power_manager.cpp
    ${PROJECT_SOURCE_DIR}/src/app/power_config.cpp
    ${PROJECT_SOURCE_DIR}/src/app/device_class.cpp
    ${PROJECT_SOURCE_DIR}/src/app/ota_activity.cpp
)
target_include_directories(test_power_manager BEFORE PRIVATE
    ${PROJECT_SOURCE_DIR}/tests/power_manager_overrides ${HOST_ARDUINOJSON_DIR})
target_compile_definitions(test_power_manager PRIVATE
    OTA_ACTIVITY_HOST_TEST HAS_DISPLAY=0 HAS_BUTTON=1 HAS_BUILTIN_LED=0)
target_compile_options(test_power_manager PRIVATE -Wno-unused-variable -Wno-deprecated-declarations
    "SHELL:-include ${PROJECT_SOURCE_DIR}/tests/log_manager.h")

add_host_gtest(test_epaper_frame_wake_journal
        test_epaper_frame_wake_journal.cpp
        ${PROJECT_SOURCE_DIR}/src/app/device_classes/epaper_frame/epaper_frame_timing.cpp
)
target_compile_definitions(test_epaper_frame_wake_journal PRIVATE IS_EPAPER_FRAME=1)

add_host_gtest(test_epaper_frame_wake_budget
    test_epaper_frame_wake_budget.cpp
    ${PROJECT_SOURCE_DIR}/src/app/device_classes/epaper_frame/epaper_frame_wake_budget.cpp
)
target_compile_definitions(test_epaper_frame_wake_budget PRIVATE IS_EPAPER_FRAME=1)

add_host_gtest(test_mp3_metadata
    test_mp3_metadata.cpp
    ${PROJECT_SOURCE_DIR}/src/app/mp3_metadata.cpp
)

add_host_gtest(test_brew_template_dsl
    test_brew_template_dsl.cpp
    ${PROJECT_SOURCE_DIR}/src/app/device_classes/coffee_scale/brew/brew_template_dsl.cpp
)
target_compile_definitions(test_brew_template_dsl PRIVATE HAS_SCALE=1)
target_include_directories(test_brew_template_dsl PRIVATE
    ${PROJECT_SOURCE_DIR}/src/app/device_classes/coffee_scale/brew
    $ENV{HOME}/Arduino/libraries/ArduinoJson/src
)