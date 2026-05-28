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

echo "=== Building unit tests: widget_common ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -I src/app \
    tests/test_widget_common.cpp \
    -o tests/bin/test_widget_common -lm

echo "=== Running unit tests: widget_common ==="
./tests/bin/test_widget_common
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

echo "=== Running guard: branding mirror (C++ <-> bash) ==="
./tests/test_branding_mirror.sh
echo

echo "=== All tests passed ==="
