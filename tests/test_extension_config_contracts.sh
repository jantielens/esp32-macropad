#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

require() {
    local pattern=$1
    local file=$2
    grep -Fq -- "$pattern" "$file" || {
        echo "Missing configuration contract '$pattern' in $file" >&2
        exit 1
    }
}

for extension in block-drop-clock brick-breaker-clock nixie-clock word-clock; do
    require '"time"' "extensions/$extension/${extension//-/_}.cpp"
    jq -er '.usage | contains("\"time\"")' "extensions/$extension/metadata.json" >/dev/null
done

require 'find_string(config_json, "time"' extensions/matrix-rain/matrix_rain.cpp
require 'find_string(config_json, "clock"' extensions/matrix-rain/matrix_rain.cpp
jq -er '.usage | contains("\"time\"") and (contains("\"clock\"") | not)' extensions/matrix-rain/metadata.json >/dev/null

require 'uint16_t parse_speed_percent(const char* json)' extensions/brick-breaker-clock/brick_breaker_clock.cpp
require 'instance->speed_percent = parse_speed_percent(config_json);' extensions/brick-breaker-clock/brick_breaker_clock.cpp
jq -er '.usage | contains("\"speed\":1.0")' extensions/brick-breaker-clock/metadata.json >/dev/null