#!/bin/bash
# Guard explicit task-stack placement helpers and the audio worker migration.

set -e
cd "$(dirname "$0")/.."

HEADER="src/app/rtos_task_utils.h"
SOURCE="src/app/rtos_task_utils.cpp"
AUDIO="src/app/audio.cpp"

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

echo "PASS: explicit task-stack helpers and audio migration are present."