#!/bin/bash
# ============================================================================
# Run all host-native unit and integration tests
# ============================================================================
# No ESP32 needed — compiles and runs on the development machine.
#
# Usage:
#   ./tests/run_tests.sh              # Build and run all tests
#   ./tests/run_tests.sh --coverage   # Same, plus gcov line-coverage report

set -e
cd "$(dirname "$0")/.."

# ---------------------------------------------------------------------------
# Parse flags
# ---------------------------------------------------------------------------
COVERAGE=false
if [[ "${1:-}" == "--coverage" ]]; then
    COVERAGE=true
fi

# ---------------------------------------------------------------------------
# Output directory
# ---------------------------------------------------------------------------
if $COVERAGE; then
    BIN_DIR=tests/bin/coverage
    rm -rf "$BIN_DIR"
else
    BIN_DIR=tests/bin
fi
mkdir -p "$BIN_DIR"

# ---------------------------------------------------------------------------
# Compiler flags
# ---------------------------------------------------------------------------
if $COVERAGE; then
    CXXFLAGS="-std=c++17 -Wall -Wextra --coverage -O0 -g"
else
    CXXFLAGS="-std=c++17 -Wall -Wextra -Werror"
fi

# ---------------------------------------------------------------------------
# Locate ArduinoJson headers (installed by setup.sh into ~/Arduino/libraries)
# ---------------------------------------------------------------------------
ARDUINOJSON_INC=""
for candidate in \
    "$HOME/Arduino/libraries/ArduinoJson/src" \
    "$HOME/Arduino/libraries/ArduinoJson"; do
    if [ -f "$candidate/ArduinoJson.h" ]; then
        ARDUINOJSON_INC="$candidate"
        break
    fi
done

# Include flags
INC="-include tests/log_manager.h -include tests/board_config.h -I tests -I src/app"
if [ -n "$ARDUINOJSON_INC" ]; then
    INC="$INC -I $ARDUINOJSON_INC"
fi

# ---------------------------------------------------------------------------
# Coverage mode: compile shared source files ONCE as object files so gcov
# data accumulates across every test binary that links them.
# ---------------------------------------------------------------------------
if $COVERAGE; then
    echo "=== Compiling shared objects (coverage) ==="
    COV_SOURCES=(
        src/app/expr_eval.cpp
        src/app/binding_template.cpp
        src/app/pad_binding.cpp
        src/app/key_sequence.cpp
        tests/stubs.cpp
    )
    if [ -n "$ARDUINOJSON_INC" ]; then
        COV_SOURCES+=(
            src/app/health_table_builder.cpp
            src/app/brew_manager.cpp
            src/app/brew_templates.cpp
            src/app/brew_template_dsl.cpp
            src/app/brew_binding.cpp
        )
    fi
    for src in "${COV_SOURCES[@]}"; do
        obj="$BIN_DIR/$(basename "${src%.cpp}.o")"
        g++ $CXXFLAGS $INC -c "$src" -o "$obj"
    done
    OBJ() { echo "$BIN_DIR/$1.o"; }
fi

# ---------------------------------------------------------------------------
# Helper: build & run a test suite
# ---------------------------------------------------------------------------
build_and_run() {
    local name=$1; shift
    echo "=== Building: $name ==="
    g++ $CXXFLAGS "$@" -o "$BIN_DIR/test_$name"
    echo "=== Running: $name ==="
    "$BIN_DIR/test_$name"
    echo
}

# ===== expr_eval =====
if $COVERAGE; then
    build_and_run expr_eval -I src/app \
        tests/test_expr_eval.cpp $(OBJ expr_eval) -lm
else
    build_and_run expr_eval \
        tests/test_expr_eval.cpp src/app/expr_eval.cpp -lm
fi

# ===== binding_template =====
if $COVERAGE; then
    build_and_run binding_template $INC \
        tests/test_binding_template.cpp $(OBJ binding_template) $(OBJ stubs) -lm
else
    build_and_run binding_template \
        -include tests/log_manager.h -include tests/board_config.h -I src/app \
        tests/test_binding_template.cpp src/app/binding_template.cpp \
        tests/stubs.cpp -lm
fi

# ===== health_table_builder (requires ArduinoJson) =====
if [ -n "$ARDUINOJSON_INC" ]; then
    if $COVERAGE; then
        build_and_run health_table_builder $INC \
            tests/test_health_table_builder.cpp $(OBJ health_table_builder) $(OBJ stubs) -lm
    else
        build_and_run health_table_builder \
            -include tests/log_manager.h -include tests/board_config.h \
            -I src/app -I "$ARDUINOJSON_INC" \
            tests/test_health_table_builder.cpp src/app/health_table_builder.cpp \
            tests/stubs.cpp -lm
    fi
else
    echo "WARNING: ArduinoJson headers not found — skipping health_table_builder tests"
fi

# ===== expr_binding =====
if $COVERAGE; then
    build_and_run expr_binding $INC \
        tests/test_expr_binding.cpp $(OBJ binding_template) $(OBJ expr_eval) $(OBJ stubs) -lm
else
    build_and_run expr_binding \
        -include tests/log_manager.h -include tests/board_config.h -I src/app \
        tests/test_expr_binding.cpp src/app/binding_template.cpp src/app/expr_eval.cpp \
        tests/stubs.cpp -lm
fi

# ===== pad_binding =====
if $COVERAGE; then
    build_and_run pad_binding $INC \
        tests/test_pad_binding.cpp $(OBJ binding_template) $(OBJ pad_binding) $(OBJ expr_eval) $(OBJ stubs) -lm
else
    build_and_run pad_binding \
        -include tests/log_manager.h -include tests/board_config.h -I src/app \
        tests/test_pad_binding.cpp src/app/binding_template.cpp src/app/pad_binding.cpp \
        src/app/expr_eval.cpp tests/stubs.cpp -lm
fi

# ===== widget_common =====
build_and_run widget_common -I src/app \
    tests/test_widget_common.cpp -lm

# ===== key_sequence =====
if $COVERAGE; then
    build_and_run key_sequence -I src/app \
        tests/test_key_sequence.cpp $(OBJ key_sequence)
else
    build_and_run key_sequence \
        tests/test_key_sequence.cpp src/app/key_sequence.cpp
fi

# ===== scale_smoothing =====
if $COVERAGE; then
    build_and_run scale_smoothing -I tests -I src/app \
        tests/test_scale_smoothing.cpp src/app/sensors/scale_smoothing.cpp -lm
else
    build_and_run scale_smoothing \
        -I tests -I src/app \
        tests/test_scale_smoothing.cpp src/app/sensors/scale_smoothing.cpp -lm
fi

# ===== Brew tests (require ArduinoJson) =====
if [ -z "$ARDUINOJSON_INC" ]; then
    echo "WARNING: ArduinoJson headers not found — skipping brew tests"
else
    # ===== brew_template_dsl =====
    if $COVERAGE; then
        build_and_run brew_template_dsl $INC \
            tests/test_brew_template_dsl.cpp $(OBJ brew_template_dsl) $(OBJ stubs)
    else
        build_and_run brew_template_dsl \
            -include tests/log_manager.h -include tests/board_config.h \
            -I src/app -I "$ARDUINOJSON_INC" \
            tests/test_brew_template_dsl.cpp src/app/brew_template_dsl.cpp tests/stubs.cpp
    fi

    # ===== brew_manager =====
    if $COVERAGE; then
        build_and_run brew_manager $INC \
            tests/test_brew_manager.cpp $(OBJ brew_manager) $(OBJ brew_templates) \
            $(OBJ brew_template_dsl) $(OBJ stubs) -lm
    else
        build_and_run brew_manager \
            -include tests/log_manager.h -include tests/board_config.h \
            -I tests -I src/app -I "$ARDUINOJSON_INC" \
            tests/test_brew_manager.cpp src/app/brew_manager.cpp src/app/brew_templates.cpp \
            src/app/brew_template_dsl.cpp tests/stubs.cpp -lm
    fi

    # ===== brew_binding =====
    if $COVERAGE; then
        build_and_run brew_binding $INC \
            tests/test_brew_binding.cpp $(OBJ brew_binding) $(OBJ brew_manager) \
            $(OBJ brew_templates) $(OBJ brew_template_dsl) $(OBJ stubs) -lm
    else
        build_and_run brew_binding \
            -include tests/log_manager.h -include tests/board_config.h \
            -I tests -I src/app -I "$ARDUINOJSON_INC" \
            tests/test_brew_binding.cpp src/app/brew_binding.cpp src/app/brew_manager.cpp \
            src/app/brew_templates.cpp src/app/brew_template_dsl.cpp tests/stubs.cpp -lm
    fi
fi

echo "=== Building unit tests: action_parse ==="
g++ -std=c++17 -Wall -Wextra -Werror -Wno-deprecated-declarations \
    -include tests/log_manager.h -include tests/board_config.h \
    -I src/app \
    -I ~/Arduino/libraries/ArduinoJson/src \
    tests/test_action_parse.cpp \
    src/app/action_parse.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_action_parse

echo "=== Running unit tests: action_parse ==="
./tests/bin/test_action_parse
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

echo "=== All tests passed ==="

# ---------------------------------------------------------------------------
# Coverage report (only with --coverage)
# ---------------------------------------------------------------------------
if $COVERAGE; then
    echo
    echo "=== Coverage Report ==="
    echo

    # Run gcov from project root so it resolves source paths in .gcno files
    gcov -o "$BIN_DIR" "$BIN_DIR"/*.gcda > /dev/null 2>&1
    mv -f *.gcov "$BIN_DIR/" 2>/dev/null || true

    SOURCES="expr_eval.cpp binding_template.cpp pad_binding.cpp key_sequence.cpp"
    if [ -n "$ARDUINOJSON_INC" ]; then
        SOURCES="$SOURCES health_table_builder.cpp brew_manager.cpp brew_binding.cpp brew_templates.cpp brew_template_dsl.cpp"
    fi

    echo "File                        Lines    Exec  Missed  Cover"
    echo "---------------------------------------------------------"

    total_lines=0
    total_exec=0

    for src in $SOURCES; do
        gcov_file="$BIN_DIR/${src}.gcov"
        if [ -f "$gcov_file" ]; then
            exec_lines=$(grep -cE '^\s+[0-9]+:' "$gcov_file" || true)
            missed=$(grep -cE '^\s+#####:' "$gcov_file" || true)
            exec_lines=${exec_lines:-0}
            missed=${missed:-0}
            total=$((exec_lines + missed))
            if [ "$total" -gt 0 ]; then
                pct=$((exec_lines * 100 / total))
            else
                pct=0
            fi
            printf "%-28s %5d   %5d   %5d   %3d%%\n" "$src" "$total" "$exec_lines" "$missed" "$pct"
            total_lines=$((total_lines + total))
            total_exec=$((total_exec + exec_lines))
        else
            printf "%-28s   (no coverage data)\n" "$src"
        fi
    done

    echo "---------------------------------------------------------"
    if [ "$total_lines" -gt 0 ]; then
        total_pct=$((total_exec * 100 / total_lines))
    else
        total_pct=0
    fi
    total_missed=$((total_lines - total_exec))
    printf "%-28s %5d   %5d   %5d   %3d%%\n" "TOTAL" "$total_lines" "$total_exec" "$total_missed" "$total_pct"

    echo
    echo "Detailed .gcov files: $BIN_DIR/"
    echo "Show uncovered lines: grep -n '#####' $BIN_DIR/<file>.gcov"

fi