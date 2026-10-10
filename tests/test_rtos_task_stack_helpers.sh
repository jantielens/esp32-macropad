#!/bin/bash
# Guard explicit task-stack placement helpers and the audio worker migration.

set -e
cd "$(dirname "$0")/.."

HEADER="src/app/rtos_task_utils.h"
SOURCE="src/app/rtos_task_utils.cpp"
AUDIO="src/app/audio.cpp"
DISPLAY="src/app/display_manager.cpp"

grep -q 'rtos_create_task_internal_stack(' "$HEADER"
grep -q 'rtos_create_task_internal_stack_pinned(' "$HEADER"
grep -q 'LittleFS, Preferences/NVS, OTA' "$HEADER"
grep -q 'MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT' "$SOURCE"
grep -q 'esp_ptr_internal(stackStart)' "$SOURCE"
grep -q 'esp_ptr_internal(stackEnd)' "$SOURCE"
grep -q 'rtos_create_task_internal_stack_pinned(' "$AUDIO"

if grep -q 'xTaskCreatePinnedToCoreWithCaps(' "$AUDIO"; then
    echo "FAIL: audio task must use the shared internal-stack helper" >&2
    exit 1
fi

for unicore in 0 1; do
    for psram_stack in 0 1; do
        for ttf in 0 1; do
            for sd in 0 1; do
                stack_path=$(awk '
                    /static constexpr uint32_t kLvglStackBytes/ { capture = 1 }
                    /Create async present task/ { capture = 0 }
                    capture
                ' "$DISPLAY" | c++ -E -P -x c++ \
                    -DCONFIG_FREERTOS_UNICORE="$unicore" \
                    -DLVGL_TASK_USE_PSRAM_STACK="$psram_stack" \
                    -DHAS_TTF_FONTS="$ttf" -DUSE_SD_STORAGE="$sd" -)
                expected_helper="rtos_create_task_internal_stack"
                if [[ "$psram_stack" == 1 && ( "$ttf" == 0 || "$sd" == 1 ) ]]; then
                    expected_helper="rtos_create_task_psram_stack"
                fi
                if [[ "$unicore" == 0 ]]; then
                    expected_helper="${expected_helper}_pinned"
                fi
                if ! grep -Fq "${expected_helper}(" <<< "$stack_path"; then
                    echo "FAIL: LVGL stack selection unicore=$unicore psram=$psram_stack ttf=$ttf sd=$sd" >&2
                    exit 1
                fi
            done
        done
    done
done

echo "PASS: explicit task-stack helpers, audio migration, and cache-safe LVGL font loading."