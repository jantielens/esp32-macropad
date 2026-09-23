#!/bin/bash
# =============================================================================
# E-Paper Frame portal configuration contract guard
# =============================================================================

set -euo pipefail
cd "$(dirname "$0")/.."

BACKEND="src/app/device_classes/epaper_frame_device_class.cpp"
PORTAL="src/app/device_classes/epaper_frame/web/epaper_init.js"
IMAGE_FRAGMENT="src/app/device_classes/epaper_frame/web/epaper-image.fragment.html"
OVERLAY_FRAGMENT="src/app/device_classes/epaper_frame/web/epaper-overlay.fragment.html"

require() {
    local pattern=$1
    local file=$2
    grep -Fq -- "$pattern" "$file" || {
        echo "FAIL: missing '$pattern' in $file" >&2
        exit 1
    }
}

for key in \
    epaper_frame_source_mode \
    epaper_frame_service_url \
    epaper_frame_service_token \
    epaper_frame_service_interval_seconds \
    epaper_frame_offline_refreshes_between_syncs \
    epaper_frame_wake_budget_ms \
    epaper_frame_wake_wifi_target_ms \
    epaper_frame_wake_wifi_budget_ms \
    epaper_frame_wake_fetch_target_ms \
    epaper_frame_wake_fetch_budget_ms \
    epaper_frame_wake_mqtt_target_ms \
    epaper_frame_wake_mqtt_budget_ms \
    epaper_frame_wake_cutoff_retry_seconds \
    epaper_frame_rotation \
    epaper_frame_crc32_enabled \
    epaper_frame_sd_cache_enabled \
    epaper_frame_frontlight_brightness \
    epaper_frame_frontlight_duration_s; do
    require "$key" "$BACKEND"
    require "$key" "$PORTAL"
    require "name=\"$key\"" "$IMAGE_FRAGMENT"
done

for key in \
    epaper_frame_overlay_enabled \
    epaper_frame_overlay_position \
    epaper_frame_overlay_color \
    epaper_frame_overlay_items; do
    require "$key" "$BACKEND"
    require "$key" "$PORTAL"
    require "name=\"$key\"" "$OVERLAY_FRAGMENT"
done

for key in \
    epaper_frame_service_supported \
    epaper_frame_service_token_set \
    epaper_frame_carousel \
    epaper_frame_schedule_hours \
    epaper_frame_schedule_tz_offset; do
    require "$key" "$BACKEND"
    require "$key" "$PORTAL"
done

require 'defined(BOARD_RETERMINAL_E1003_FRAME)' "$BACKEND"
require 'Photoframe Next Image API' "$IMAGE_FRAGMENT"

if grep -R -nE '(name="epaper_(source_mode|service_|wake_|rotation|crc32_enabled|sd_cache_|overlay_|frontlight_)|cfg\.epaper_(source_mode|service_|wake_|rotation|crc32_enabled|sd_cache_|overlay_|frontlight_|carousel|schedule_))' \
    src/app/device_classes/epaper_frame/web; then
    echo "FAIL: retired E-Paper Frame portal config key found" >&2
    exit 1
fi

echo "PASS: E-Paper Frame portal configuration contract is aligned."