include(FetchContent)
include(GoogleTest)
include(CMakeParseArguments)

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
)

set(ACTION_TEST_SOURCES
    ${PROJECT_SOURCE_DIR}/src/app/actions.cpp ${PROJECT_SOURCE_DIR}/src/app/music_command.cpp
    ${PROJECT_SOURCE_DIR}/src/app/action_parse.cpp ${PROJECT_SOURCE_DIR}/src/app/action_list.cpp
    ${PROJECT_SOURCE_DIR}/src/app/action_continuation.cpp ${PROJECT_SOURCE_DIR}/src/app/pad_cycle.cpp
    ${PROJECT_SOURCE_DIR}/src/app/action_registry.cpp ${PROJECT_SOURCE_DIR}/src/app/binding_template.cpp stubs.cpp
)

add_host_legacy_test(test_action_parse
    SOURCES test_action_parse.cpp ${ACTION_TEST_SOURCES}
    INCLUDES ${HOST_ARDUINOJSON_DIR}
    OPTIONS -Wno-deprecated-declarations
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/log_manager.h ${PROJECT_SOURCE_DIR}/tests/board_config.h
)
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
add_host_legacy_test(test_camera_capture_action
    SOURCES test_camera_capture_action.cpp ${PROJECT_SOURCE_DIR}/src/app/action_continuation.cpp ${PROJECT_SOURCE_DIR}/src/app/action_registry.cpp ${PROJECT_SOURCE_DIR}/src/app/binding_template.cpp stubs.cpp
    INCLUDES ${HOST_ARDUINOJSON_DIR}
    DEFINITIONS HAS_CAMERA=1
    OPTIONS -Wno-deprecated-declarations
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/log_manager.h ${PROJECT_SOURCE_DIR}/tests/board_config.h
)
add_host_legacy_test(test_pad_cycle SOURCES test_pad_cycle.cpp ${PROJECT_SOURCE_DIR}/src/app/pad_cycle.cpp PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/board_config.h)
add_host_legacy_test(test_pad_cache_transaction SOURCES test_pad_cache_transaction.cpp ${PROJECT_SOURCE_DIR}/src/app/pad_cache_transaction.cpp PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/Arduino.h ${PROJECT_SOURCE_DIR}/tests/board_config.h)
add_host_legacy_test(test_wifi_reconnect SOURCES test_wifi_reconnect.cpp ${PROJECT_SOURCE_DIR}/src/app/wifi_reconnect.cpp)
add_host_legacy_test(test_screen_saver_schedule SOURCES test_screen_saver_schedule.cpp)
add_host_legacy_test(test_component_registry SOURCES test_component_registry.cpp ${PROJECT_SOURCE_DIR}/src/app/component_registry.cpp stubs.cpp PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/log_manager.h ${PROJECT_SOURCE_DIR}/tests/board_config.h)

add_host_legacy_test(test_timer_binding_target
    SOURCES test_timer_binding_target.cpp ${PROJECT_SOURCE_DIR}/src/app/timer_binding.cpp ${PROJECT_SOURCE_DIR}/src/app/timer_engine.cpp ${PROJECT_SOURCE_DIR}/src/app/binding_template.cpp stubs.cpp
    INCLUDES ${PROJECT_SOURCE_DIR}/tests/timer_test_overrides ${HOST_ARDUINOJSON_DIR} ${HOST_LVGL_DIR}
    OPTIONS -ffunction-sections -fdata-sections
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/timer_test_overrides/board_config.h ${PROJECT_SOURCE_DIR}/tests/log_manager.h
    LIBRARIES m pthread -Wl,--gc-sections
)
add_host_legacy_test(test_timer_commands
    SOURCES test_timer_commands.cpp ${PROJECT_SOURCE_DIR}/src/app/timer_command.cpp ${PROJECT_SOURCE_DIR}/src/app/timer_mcp_adapter.cpp ${PROJECT_SOURCE_DIR}/src/app/timer_engine.cpp stubs.cpp
    INCLUDES ${PROJECT_SOURCE_DIR}/tests/timer_test_overrides ${HOST_ARDUINOJSON_DIR}
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/timer_test_overrides/board_config.h ${PROJECT_SOURCE_DIR}/tests/log_manager.h
    LIBRARIES m pthread
)
add_test(NAME test_timer_commands_pre_init_controls COMMAND test_timer_commands pre-init-controls)
add_test(NAME test_timer_commands_mutex_failure COMMAND test_timer_commands mutex-failure)
add_host_legacy_test(test_timer_config
    SOURCES test_timer_config.cpp ${PROJECT_SOURCE_DIR}/src/app/timer_config.cpp ${PROJECT_SOURCE_DIR}/src/app/config_psram.cpp
        ${PROJECT_SOURCE_DIR}/src/app/actions.cpp ${PROJECT_SOURCE_DIR}/src/app/music_command.cpp
        ${PROJECT_SOURCE_DIR}/src/app/action_parse.cpp ${PROJECT_SOURCE_DIR}/src/app/action_continuation.cpp
        ${PROJECT_SOURCE_DIR}/src/app/pad_cycle.cpp ${PROJECT_SOURCE_DIR}/src/app/action_registry.cpp stubs.cpp
    INCLUDES ${PROJECT_SOURCE_DIR}/tests/timer_test_overrides ${HOST_ARDUINOJSON_DIR}
    OPTIONS -Wno-deprecated-declarations
    PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/timer_test_overrides/board_config.h ${PROJECT_SOURCE_DIR}/tests/log_manager.h ${PROJECT_SOURCE_DIR}/tests/Arduino.h
    LIBRARIES m pthread
)
add_test(NAME test_timer_config_config_mutex_failure COMMAND test_timer_config config-mutex-failure)
add_test(NAME test_timer_config_save_mutex_failure COMMAND test_timer_config save-mutex-failure)
add_host_legacy_test(test_timer_format SOURCES test_timer_format.cpp stubs.cpp INCLUDES ${HOST_ARDUINOJSON_DIR} PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/board_config.h)

add_host_legacy_test(test_shutter_curtain_stats SOURCES test_shutter_curtain_stats.cpp ${PROJECT_SOURCE_DIR}/src/app/device_classes/shutter_tester/shutter_curtain_stats.cpp INCLUDES ${PROJECT_SOURCE_DIR}/src/app/device_classes/shutter_tester LIBRARIES m)
add_host_legacy_test(test_shutter_capture SOURCES test_shutter_capture.cpp INCLUDES ${PROJECT_SOURCE_DIR}/src/app/device_classes/shutter_tester DEFINITIONS IS_SHUTTER_TESTER=true PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/log_manager.h)
add_host_legacy_test(test_shutter_measure SOURCES test_shutter_measure.cpp stubs.cpp INCLUDES ${PROJECT_SOURCE_DIR}/src/app/device_classes/shutter_tester DEFINITIONS IS_SHUTTER_TESTER=true PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/log_manager.h LIBRARIES m)
add_host_legacy_test(test_shutter_binding SOURCES test_shutter_binding.cpp stubs.cpp INCLUDES ${PROJECT_SOURCE_DIR}/src/app/device_classes/shutter_tester DEFINITIONS HAS_DISPLAY=true IS_SHUTTER_TESTER=true PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/log_manager.h LIBRARIES m)
add_host_legacy_test(test_shutter_session SOURCES test_shutter_session.cpp ${PROJECT_SOURCE_DIR}/src/app/device_classes/shutter_tester/shutter_session.cpp ${PROJECT_SOURCE_DIR}/src/app/device_classes/shutter_tester/shutter_curtain_stats.cpp stubs.cpp INCLUDES ${PROJECT_SOURCE_DIR}/tests/shutter_session_overrides ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/src/app/device_classes/shutter_tester ${HOST_ARDUINOJSON_DIR} DEFINITIONS IS_SHUTTER_TESTER=true OPTIONS -Wno-deprecated-declarations PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/log_manager.h LIBRARIES m)
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
add_host_legacy_test(test_epaper_battery SOURCES test_epaper_battery.cpp)
add_host_legacy_test(test_epaper_media_conformance SOURCES test_epaper_media_conformance.cpp ${PROJECT_SOURCE_DIR}/src/app/device_classes/epaper/epaper_media_validation.cpp ${PROJECT_SOURCE_DIR}/src/app/device_classes/epaper/epaper_transport_crc32.cpp LIBRARIES z)
add_host_legacy_test(test_coffee_scale_cmd_len SOURCES test_coffee_scale_cmd_len.cpp DEFINITIONS IS_COFFEE_SCALE=true PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/board_config.h)
add_host_legacy_test(test_expose_dry_down SOURCES test_expose_dry_down.cpp INCLUDES ${PROJECT_SOURCE_DIR}/src/app/device_classes/darkroom_timer DEFINITIONS IS_DARKROOM_TIMER=1 HAS_AUDIO=0 LIBRARIES m)
add_host_legacy_test(test_meter_mag SOURCES test_meter_mag.cpp INCLUDES ${PROJECT_SOURCE_DIR}/src/app/device_classes/darkroom_timer DEFINITIONS IS_DARKROOM_TIMER=1 LIBRARIES m)
add_host_legacy_test(test_print_log SOURCES test_print_log.cpp INCLUDES ${PROJECT_SOURCE_DIR}/tests/print_log_overrides ${PROJECT_SOURCE_DIR}/src/app/device_classes/darkroom_timer ${HOST_ARDUINOJSON_DIR} DEFINITIONS IS_DARKROOM_TIMER=1 LIBRARIES m)
add_host_legacy_test(test_mcp_result_strings SOURCES test_mcp_result_strings.cpp INCLUDES ${HOST_ARDUINOJSON_DIR})
add_host_legacy_test(test_mcp_device_identity SOURCES test_mcp_device_identity.cpp ${PROJECT_SOURCE_DIR}/src/app/mcp_device_identity.cpp INCLUDES ${HOST_ARDUINOJSON_DIR} DEFINITIONS HAS_MCP=1 PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/Arduino.h ${PROJECT_SOURCE_DIR}/tests/board_config.h)
add_host_legacy_test(test_fs_health_littlefs SOURCES test_fs_health.cpp ${PROJECT_SOURCE_DIR}/src/app/fs_health.cpp DEFINITIONS USE_SD_STORAGE=0 PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/board_config.h)
add_host_legacy_test(test_fs_health_sdmmc SOURCES test_fs_health.cpp ${PROJECT_SOURCE_DIR}/src/app/fs_health.cpp DEFINITIONS USE_SD_STORAGE=1 HAS_SD_CARD=1 PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/board_config.h)
add_host_legacy_test(test_fs_health_telemetry SOURCES test_fs_health_telemetry.cpp INCLUDES ${HOST_ARDUINOJSON_DIR} PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/Arduino.h ${PROJECT_SOURCE_DIR}/tests/board_config.h)
add_host_legacy_test(test_storage_policy_littlefs SOURCES test_storage_policy.cpp INCLUDES ${PROJECT_SOURCE_DIR}/tests/storage_policy_overrides DEFINITIONS USE_SD_STORAGE=0)
add_host_legacy_test(test_storage_policy_sdmmc SOURCES test_storage_policy.cpp INCLUDES ${PROJECT_SOURCE_DIR}/tests/storage_policy_overrides DEFINITIONS USE_SD_STORAGE=1 HAS_SD_CARD=1)
add_host_legacy_test(test_factory_reset_storage SOURCES test_factory_reset_storage.cpp INCLUDES ${PROJECT_SOURCE_DIR}/tests/factory_reset_overrides OPTIONS -ffunction-sections -fdata-sections LIBRARIES -Wl,--gc-sections)

function(add_binding_schema_profile name definition)
    add_host_legacy_test(test_binding_schema_${name}
        SOURCES test_binding_schema_production.cpp binding_schema_profiles/stubs.cpp
            ${PROJECT_SOURCE_DIR}/src/app/binding_template.cpp ${PROJECT_SOURCE_DIR}/src/app/binding_builtin_schemes.cpp
            ${PROJECT_SOURCE_DIR}/src/app/health_binding.cpp ${PROJECT_SOURCE_DIR}/src/app/timer_binding.cpp
            ${PROJECT_SOURCE_DIR}/src/app/music_binding.cpp ${PROJECT_SOURCE_DIR}/src/app/net_binding.cpp
            ${PROJECT_SOURCE_DIR}/src/app/list_binding.cpp ${PROJECT_SOURCE_DIR}/src/app/list_provider.cpp
            ${PROJECT_SOURCE_DIR}/src/app/expr_binding.cpp ${PROJECT_SOURCE_DIR}/src/app/pad_binding.cpp
            ${PROJECT_SOURCE_DIR}/src/app/camera_binding.cpp ${PROJECT_SOURCE_DIR}/src/app/expr_eval.cpp
            ${PROJECT_SOURCE_DIR}/src/app/time_binding.cpp ${ARGN}
        INCLUDES ${PROJECT_SOURCE_DIR}/tests/binding_schema_profiles ${PROJECT_SOURCE_DIR}/src
        DEFINITIONS ${definition}
        OPTIONS -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function
        PREINCLUDE ${PROJECT_SOURCE_DIR}/tests/Arduino.h ${PROJECT_SOURCE_DIR}/tests/binding_schema_profiles/platform_stubs.h
    )
endfunction()

add_binding_schema_profile(baseline "")
add_binding_schema_profile(camera BINDING_SCHEMA_PROFILE_CAMERA)
add_binding_schema_profile(full BINDING_SCHEMA_PROFILE_FULL ${PROJECT_SOURCE_DIR}/src/app/audio_input_binding.cpp)
add_binding_schema_profile(voice BINDING_SCHEMA_PROFILE_VOICE ${PROJECT_SOURCE_DIR}/src/app/device_classes/voice_assistant/voice_binding.cpp)
add_binding_schema_profile(coffee BINDING_SCHEMA_PROFILE_COFFEE ${PROJECT_SOURCE_DIR}/src/app/device_classes/coffee_scale/scale_binding.cpp ${PROJECT_SOURCE_DIR}/src/app/device_classes/coffee_scale/brew/brew_binding.cpp)
add_binding_schema_profile(darkroom BINDING_SCHEMA_PROFILE_DARKROOM ${PROJECT_SOURCE_DIR}/src/app/device_classes/darkroom_timer/print_log.cpp ${PROJECT_SOURCE_DIR}/src/app/device_classes/darkroom_timer/meter.cpp ${PROJECT_SOURCE_DIR}/src/app/device_classes/darkroom_timer/expose_timer.cpp ${PROJECT_SOURCE_DIR}/src/app/device_classes/darkroom_timer/test_strip.cpp)
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
    test_brew_storage_and_tare_guards.sh test_branding_mirror.sh test_asset_variant_matrix.sh
    test_camera_preview_widget.sh test_camera_motion.sh test_mcp_scheme_parity.sh
    test_action_catalog_completeness.sh test_mcp_storage_parity.sh test_display_safe_telemetry.sh
    test_sd_wifi_order.sh test_photoframe_vector_drift.sh
)
    get_filename_component(name ${script} NAME_WE)
    add_host_command_test(${name} bash ${PROJECT_SOURCE_DIR}/tests/${script})
endforeach()

add_host_command_test(test_portal_component_capacity_matrix python3 ${PROJECT_SOURCE_DIR}/tests/test_portal_component_capacity_matrix.py)
add_host_command_test(test_widget_schema python3 ${PROJECT_SOURCE_DIR}/tools/lint_widget_schema.py)
add_host_command_test(test_timer_mcp_integration python3 ${PROJECT_SOURCE_DIR}/tests/test_timer_mcp_integration.py)

foreach(script
    test_timer_action_editor.js test_portal_action_editor_cycle.js test_portal_action_picker.js
    test_portal_action_list.js test_portal_timer_binding.js test_portal_pad_dialog_transaction.js
    test_portal_pad_import.js test_portal_pad_dirty.js
)
    get_filename_component(name ${script} NAME_WE)
    add_host_command_test(${name} node ${PROJECT_SOURCE_DIR}/tests/${script})
endforeach()

foreach(script portal_action_editor.js portal_binding_validator.js portal_pad_dialog.js portal_config_actions.js)
    get_filename_component(name ${script} NAME_WE)
    add_host_command_test(check_${name}_syntax node --check ${PROJECT_SOURCE_DIR}/src/app/web/${script})
endforeach()

add_host_gtest(test_epaper_carousel
    test_epaper_carousel.cpp
    ${PROJECT_SOURCE_DIR}/src/app/device_classes/epaper/epaper_carousel.cpp
)
target_compile_definitions(test_epaper_carousel PRIVATE HAS_EPAPER=1)

add_host_gtest(test_epaper_schedule
    test_epaper_schedule.cpp
    ${PROJECT_SOURCE_DIR}/src/app/device_classes/epaper/epaper_schedule.cpp
)
target_compile_definitions(test_epaper_schedule PRIVATE HAS_EPAPER=1)

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

add_host_gtest(test_epaper_next_client_logic
    test_epaper_next_client_logic.cpp
    ${PROJECT_SOURCE_DIR}/src/app/device_classes/epaper/epaper_next_client_logic.cpp
    ${PROJECT_SOURCE_DIR}/src/app/device_classes/epaper/epaper_transport_crc32.cpp
)

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