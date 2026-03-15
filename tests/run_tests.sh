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

echo "=== All tests passed ==="
