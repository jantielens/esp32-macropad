#!/bin/bash
# ============================================================================
# Run all host-native unit and integration tests
# ============================================================================
# No ESP32 needed — compiles and runs on the development machine.
# Usage: ./tests/run_tests.sh

set -e
cd "$(dirname "$0")/.."

mkdir -p tests/bin

echo "=== Building unit tests: expr_eval ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    tests/test_expr_eval.cpp \
    src/app/expr_eval.cpp \
    -o tests/bin/test_expr_eval -lm

echo "=== Running unit tests: expr_eval ==="
./tests/bin/test_expr_eval
echo

echo "=== Building integration tests: expr_binding ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -include tests/log_manager.h -include tests/board_config.h \
    -I src/app \
    tests/test_expr_binding.cpp \
    src/app/binding_template.cpp \
    src/app/expr_eval.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_expr_binding -lm

echo "=== Running integration tests: expr_binding ==="
./tests/bin/test_expr_binding
echo

echo "=== Building unit tests: binding_template ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -include tests/log_manager.h -include tests/board_config.h \
    -I src/app \
    tests/test_binding_template.cpp \
    src/app/binding_template.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_binding_template -lm

echo "=== Running unit tests: binding_template ==="
./tests/bin/test_binding_template
echo

echo "=== Building unit tests: health_table_builder ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -include tests/log_manager.h -include tests/board_config.h \
    -I src/app \
    -I ~/Arduino/libraries/ArduinoJson/src \
    tests/test_health_table_builder.cpp \
    src/app/health_table_builder.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_health_table_builder -lm

echo "=== Running unit tests: health_table_builder ==="
./tests/bin/test_health_table_builder
echo

echo "=== Building integration tests: pad_binding ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -include tests/log_manager.h -include tests/board_config.h \
    -I src/app \
    tests/test_pad_binding.cpp \
    src/app/binding_template.cpp \
    src/app/pad_binding.cpp \
    src/app/expr_eval.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_pad_binding -lm

echo "=== Running integration tests: pad_binding ==="
./tests/bin/test_pad_binding
echo

echo "=== Building unit tests: label_style ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -include tests/log_manager.h -include tests/board_config.h \
    -I src/app \
    tests/test_label_style.cpp \
    src/app/label_style.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_label_style -lm

echo "=== Running unit tests: label_style ==="
./tests/bin/test_label_style
echo

echo "=== Building unit tests: pad_confirmation ==="
g++ -std=c++17 -Wall -Wextra -Werror -Wno-deprecated-declarations \
    -include tests/log_manager.h -include tests/board_config.h \
    -I tests -I src/app \
    -I ~/Arduino/libraries/ArduinoJson/src \
    -I ~/Arduino/libraries/lvgl/src \
    tests/test_pad_confirmation.cpp \
    src/app/pad_validate.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_pad_confirmation

echo "=== Running unit tests: pad_confirmation ==="
./tests/bin/test_pad_confirmation
echo

echo "=== Building unit tests: widget_common ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -I src/app \
    tests/test_widget_common.cpp \
    -o tests/bin/test_widget_common -lm

echo "=== Running unit tests: widget_common ==="
./tests/bin/test_widget_common
echo

echo "=== Building unit tests: minimp3_scratch ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -I src/app \
    tests/test_minimp3_scratch.cpp \
    -o tests/bin/test_minimp3_scratch -lm

echo "=== Running unit tests: minimp3_scratch ==="
./tests/bin/test_minimp3_scratch
echo

echo "=== Building unit tests: key_sequence ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    tests/test_key_sequence.cpp \
    src/app/key_sequence.cpp \
    -o tests/bin/test_key_sequence

echo "=== Running unit tests: key_sequence ==="
./tests/bin/test_key_sequence
echo

echo "=== Building unit tests: action_parse ==="
g++ -std=c++17 -Wall -Wextra -Werror -Wno-deprecated-declarations \
    -include tests/log_manager.h -include tests/board_config.h \
    -I src/app \
    -I ~/Arduino/libraries/ArduinoJson/src \
    tests/test_action_parse.cpp \
    src/app/action_parse.cpp \
    src/app/action_registry.cpp \
    src/app/binding_template.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_action_parse

echo "=== Running unit tests: action_parse ==="
./tests/bin/test_action_parse
echo

echo "=== Building size guard: action_sizes ==="
g++ -std=c++17 -Wall -Wextra -Wno-unused \
    -include tests/log_manager.h -include tests/board_config.h \
    -I src/app \
    -I ~/Arduino/libraries/ArduinoJson/src \
    tests/test_action_sizes.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_action_sizes

echo "=== Running size guard: action_sizes ==="
./tests/bin/test_action_sizes
echo

echo "=== Building unit tests: shutter_session_actions ==="
g++ -std=c++17 -Wall -Wextra -Werror -Wno-deprecated-declarations \
    -include tests/log_manager.h -include tests/board_config.h \
    -I src/app -I src/app/device_classes/shutter_tester \
    -I ~/Arduino/libraries/ArduinoJson/src \
    tests/test_shutter_session_actions.cpp \
    src/app/action_parse.cpp \
    src/app/action_registry.cpp \
    src/app/binding_template.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_shutter_session_actions

echo "=== Running unit tests: shutter_session_actions ==="
./tests/bin/test_shutter_session_actions
echo

echo "=== Building unit tests: wifi_reconnect ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -I tests -I src/app \
    tests/test_wifi_reconnect.cpp \
    src/app/wifi_reconnect.cpp \
    -o tests/bin/test_wifi_reconnect

echo "=== Running unit tests: wifi_reconnect ==="
./tests/bin/test_wifi_reconnect
echo

echo "=== Building unit tests: timer_format ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -include tests/board_config.h \
    -I tests -I src/app \
    -I ~/Arduino/libraries/ArduinoJson/src \
    tests/test_timer_format.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_timer_format

echo "=== Running unit tests: timer_format ==="
./tests/bin/test_timer_format
echo

echo "=== Building unit tests: action_bindings ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -include tests/log_manager.h -include tests/board_config.h \
    -I src/app \
    tests/test_action_bindings.cpp \
    src/app/binding_template.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_action_bindings -lm

echo "=== Running unit tests: action_bindings ==="
./tests/bin/test_action_bindings
echo

echo "=== Building unit tests: action_registry_value_field ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -include tests/log_manager.h -include tests/board_config.h \
    -I src/app \
    -I ~/Arduino/libraries/ArduinoJson/src \
    tests/test_action_registry_value_field.cpp \
    src/app/action_registry.cpp \
    src/app/binding_template.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_action_registry_value_field -lm

echo "=== Running unit tests: action_registry_value_field ==="
./tests/bin/test_action_registry_value_field
echo

echo "=== Building unit tests: component_registry ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -include tests/log_manager.h -include tests/board_config.h \
    -I tests -I src/app \
    tests/test_component_registry.cpp \
    src/app/component_registry.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_component_registry

echo "=== Running unit tests: component_registry ==="
./tests/bin/test_component_registry
echo

echo "=== Building unit tests: shutter_curtain_stats ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -I src/app -I src/app/device_classes/shutter_tester \
    tests/test_shutter_curtain_stats.cpp \
    src/app/device_classes/shutter_tester/shutter_curtain_stats.cpp \
    -o tests/bin/test_shutter_curtain_stats -lm

echo "=== Running unit tests: shutter_curtain_stats ==="
./tests/bin/test_shutter_curtain_stats
echo

echo "=== Building unit tests: shutter_capture ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -DIS_SHUTTER_TESTER=true \
    -include tests/log_manager.h \
    -I tests -I src/app -I src/app/device_classes/shutter_tester \
    tests/test_shutter_capture.cpp \
    -o tests/bin/test_shutter_capture

echo "=== Running unit tests: shutter_capture ==="
./tests/bin/test_shutter_capture
echo

echo "=== Building unit tests: shutter_measure ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -DIS_SHUTTER_TESTER=true \
    -include tests/log_manager.h \
    -I tests -I src/app -I src/app/device_classes/shutter_tester \
    tests/test_shutter_measure.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_shutter_measure -lm

echo "=== Running unit tests: shutter_measure ==="
./tests/bin/test_shutter_measure
echo

echo "=== Building unit tests: shutter_binding ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -DHAS_DISPLAY=true -DIS_SHUTTER_TESTER=true \
    -include tests/log_manager.h \
    -I tests -I src/app -I src/app/device_classes/shutter_tester \
    tests/test_shutter_binding.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_shutter_binding -lm

echo "=== Running unit tests: shutter_binding ==="
./tests/bin/test_shutter_binding
echo

echo "=== Building unit tests: list_provider ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -include tests/log_manager.h -include tests/board_config.h \
    -I src/app \
    tests/test_list_provider.cpp \
    src/app/list_provider.cpp \
    src/app/list_binding.cpp \
    src/app/binding_template.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_list_provider

echo "=== Running unit tests: list_provider ==="
./tests/bin/test_list_provider
echo

echo "=== Building unit tests: net_activity ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -I tests -I src/app \
    tests/test_net_activity.cpp \
    src/app/net_activity.cpp \
    -o tests/bin/test_net_activity

echo "=== Running unit tests: net_activity ==="
./tests/bin/test_net_activity
echo

echo "=== Building unit tests: shutter_session ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -Wno-deprecated-declarations \
    -DIS_SHUTTER_TESTER=true \
    -include tests/log_manager.h \
    -I tests/shutter_session_overrides \
    -I tests \
    -I src -I src/app -I src/app/device_classes/shutter_tester \
    -I ~/Arduino/libraries/ArduinoJson/src \
    tests/test_shutter_session.cpp \
    src/app/device_classes/shutter_tester/shutter_session.cpp \
    src/app/device_classes/shutter_tester/shutter_curtain_stats.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_shutter_session -lm

echo "=== Running unit tests: shutter_session ==="
./tests/bin/test_shutter_session
echo

echo "=== Building unit tests: epaper_battery ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -I src/app \
    tests/test_epaper_battery.cpp \
    -o tests/bin/test_epaper_battery

echo "=== Running unit tests: epaper_battery ==="
./tests/bin/test_epaper_battery
echo

echo "=== Building unit tests: epaper_assignment ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -I src/app \
    tests/test_epaper_assignment.cpp \
    src/app/device_classes/epaper/epaper_assignment_logic.cpp \
    src/app/device_classes/epaper/epaper_source_mode.cpp \
    -o tests/bin/test_epaper_assignment

echo "=== Running unit tests: epaper_assignment ==="
./tests/bin/test_epaper_assignment
echo

echo "=== Building unit tests: epaper_ble_codec (ASan/UBSan) ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -DHAS_BLE=1 -DEPAPER_BLE_CODEC_HOST_BACKEND \
    -I tests -I src/app -I ~/Arduino/libraries/ArduinoJson/src \
    tests/test_epaper_ble_codec.cpp \
    tests/epaper_ble_host_crypto.cpp \
    src/app/epaper_ble_codec.cpp \
    -o tests/bin/test_epaper_ble_codec

echo "=== Running unit tests: epaper_ble_codec ==="
ASAN_OPTIONS=detect_leaks=1 ./tests/bin/test_epaper_ble_codec
python3 tests/test_epaper_ble_vectors.py
echo

echo "=== Building unit tests: epaper_ble_bridge_logic ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -DHAS_BLE=1 -DEPAPER_BLE_CODEC_HOST_BACKEND \
    -I tests -I src/app \
    tests/test_epaper_ble_bridge_logic.cpp \
    tests/epaper_ble_host_crypto.cpp \
    src/app/epaper_ble_codec.cpp \
    src/app/device_classes/epaper_ble_bridge/epaper_ble_bridge_logic.cpp \
    -o tests/bin/test_epaper_ble_bridge_logic

echo "=== Running unit tests: epaper_ble_bridge_logic ==="
./tests/bin/test_epaper_ble_bridge_logic
python3 tests/test_epaper_ble_security.py
echo

echo "=== Building unit tests: epaper_ble_frame_logic ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -DHAS_BLE=1 -DEPAPER_BLE_CODEC_HOST_BACKEND \
    -I tests -I src/app \
    tests/test_epaper_ble_frame_logic.cpp \
    tests/epaper_ble_host_crypto.cpp \
    src/app/epaper_ble_codec.cpp \
    src/app/device_classes/epaper/epaper_assignment_logic.cpp \
    src/app/device_classes/epaper/epaper_ble_frame_logic.cpp \
    -o tests/bin/test_epaper_ble_frame_logic

echo "=== Running unit tests: epaper_ble_frame_logic ==="
./tests/bin/test_epaper_ble_frame_logic
echo

echo "=== Building unit tests: epaper_ble_config_budget ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -I ~/Arduino/libraries/ArduinoJson/src \
    tests/test_epaper_ble_config_budget.cpp \
    -o tests/bin/test_epaper_ble_config_budget

echo "=== Running unit tests: epaper_ble_config_budget ==="
./tests/bin/test_epaper_ble_config_budget
echo

echo "=== Building unit tests: epaper_ble_bridge_config ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -DHAS_BLE=1 -DIS_EPAPER_BLE_BRIDGE=1 \
    -DEPAPER_BLE_CODEC_HOST_BACKEND \
    -I tests -I src/app -I ~/Arduino/libraries/ArduinoJson/src \
    tests/test_epaper_ble_bridge_config.cpp \
    tests/epaper_ble_host_crypto.cpp \
    src/app/epaper_ble_codec.cpp \
    src/app/device_classes/epaper_ble_bridge/epaper_ble_bridge_logic.cpp \
    src/app/device_classes/epaper_ble_bridge/epaper_ble_bridge_config.cpp \
    -o tests/bin/test_epaper_ble_bridge_config

echo "=== Running unit tests: epaper_ble_bridge_config ==="
./tests/bin/test_epaper_ble_bridge_config
echo

echo "=== Building guard: coffee_scale command length ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -DIS_COFFEE_SCALE=true \
    -include tests/board_config.h \
    -I src/app \
    tests/test_coffee_scale_cmd_len.cpp \
    -o tests/bin/test_coffee_scale_cmd_len

echo "=== Running guard: coffee_scale command length ==="
./tests/bin/test_coffee_scale_cmd_len
echo

echo "=== Building unit tests: expose_dry_down ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -DIS_DARKROOM_TIMER=1 -DHAS_AUDIO=0 \
    -I tests -I src/app -I src/app/device_classes/darkroom_timer \
    tests/test_expose_dry_down.cpp \
    -o tests/bin/test_expose_dry_down -lm

echo "=== Running unit tests: expose_dry_down ==="
./tests/bin/test_expose_dry_down
echo

echo "=== Building unit tests: meter_mag ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -DIS_DARKROOM_TIMER=1 \
    -I tests -I src/app -I src/app/device_classes/darkroom_timer \
    tests/test_meter_mag.cpp \
    -o tests/bin/test_meter_mag -lm

echo "=== Running unit tests: meter_mag ==="
./tests/bin/test_meter_mag
echo

echo "=== Building unit tests: print_log ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -DIS_DARKROOM_TIMER=1 \
    -I tests/print_log_overrides \
    -I tests -I src/app -I src/app/device_classes/darkroom_timer \
    -I ~/Arduino/libraries/ArduinoJson/src \
    tests/test_print_log.cpp \
    -o tests/bin/test_print_log -lm

echo "=== Running unit tests: print_log ==="
./tests/bin/test_print_log
echo

echo "=== Building unit tests: mcp_result_strings ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -I ~/Arduino/libraries/ArduinoJson/src \
    tests/test_mcp_result_strings.cpp \
    -o tests/bin/test_mcp_result_strings

echo "=== Running unit tests: mcp_result_strings ==="
./tests/bin/test_mcp_result_strings
echo

echo "=== Running guard: branding mirror (C++ <-> bash) ==="
./tests/test_branding_mirror.sh
echo

echo "=== Running guard: asset bundler variant matrix ==="
./tests/test_asset_variant_matrix.sh
echo

echo "=== Running guard: widget schema parity (describe <-> parse) ==="
python3 tools/lint_widget_schema.py
echo

echo "=== Running guard: MCP binding-scheme parity (register <-> describe) ==="
./tests/test_mcp_scheme_parity.sh
echo

echo "=== All tests passed ==="
