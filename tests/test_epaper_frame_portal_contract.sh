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
require 'max="600000"' "$IMAGE_FRAGMENT"
require '5000, 600000' "$BACKEND"

node <<'NODE'
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const source = fs.readFileSync('src/app/device_classes/epaper_frame/web/epaper_init.js', 'utf8');
const calculation = source.match(/    function recalculateWakeMaximum\(\) \{[\s\S]*?\n    \}/);
assert.ok(calculation, 'wake maximum calculator exists');
const fields = {
    epaper_frame_wake_wifi_target_ms: {value: '4000'},
    epaper_frame_wake_wifi_budget_ms: {value: '5500'},
    epaper_frame_wake_fetch_target_ms: {value: '2000'},
    epaper_frame_wake_fetch_budget_ms: {value: '3500'},
    epaper_frame_wake_mqtt_target_ms: {value: '750'},
    epaper_frame_wake_mqtt_budget_ms: {value: '1500'}
};
const wakeMaximum = {value: '15000'};
const offlineRefreshes = {value: '3', disabled: false};
const context = {
    wakeMaximum, offlineRefreshes,
    sourceMode: {value: 'service'},
    sdCacheEnabled: {checked: true},
    document: {getElementById: (id) => fields[id]}
};
vm.runInNewContext(calculation[0] + '\nrecalculateWakeMaximum();', context);
assert.equal(wakeMaximum.value, '26900', 'four images are included');
fields.epaper_frame_wake_fetch_target_ms.value = '20000';
fields.epaper_frame_wake_fetch_budget_ms.value = '30000';
vm.runInNewContext('recalculateWakeMaximum();', context);
assert.equal(wakeMaximum.value, '115900', 'user timings change the estimate');
offlineRefreshes.value = '0';
vm.runInNewContext('recalculateWakeMaximum();', context);
assert.equal(wakeMaximum.value, '40900', 'zero offline images counts the first image');
offlineRefreshes.value = '';
vm.runInNewContext('recalculateWakeMaximum();', context);
assert.equal(wakeMaximum.value, '40900', 'incomplete input preserves the previous value');
NODE

node <<'NODE'
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const source = fs.readFileSync('src/app/device_classes/epaper_frame/web/epaper_init.js', 'utf8');
const calculation = source.match(/    var estimatorConstants = \{[\s\S]*?\n    \}\n\n    function estimateTimelinePosition/);
assert.ok(calculation, 'battery estimator model exists');
const fields = {
    'epaper-estimator-capacity': {value: '2000'},
    'epaper-estimator-new-photos': {value: '0'},
    'epaper-estimator-manual-refreshes': {value: '0'},
    epaper_frame_offline_refreshes_between_syncs: {value: '5'},
    epaper_frame_crc32_enabled: {checked: true},
    epaper_frame_service_interval_seconds: {value: '60'}
};
const context = {
    sourceMode: {value: 'service'},
    sdCacheEnabled: {checked: true},
    readHoursMask: () => 0xffff,
    hourEnabled: (mask, hour) => (mask & (1 << hour)) !== 0,
    document: {
        getElementById: (id) => fields[id] || null,
        querySelector: () => null
    }
};
vm.runInNewContext(calculation[0].replace(/\n    function estimateTimelinePosition[\s\S]*/, ''), context);
let model = context.estimatorModel();
assert.equal(model.scheduledWakes, 960, '16 enabled hours at 60 seconds');
assert.equal(model.panelRefreshes, 960, 'queued service images always refresh the panel');
assert.equal(model.crcApplies, false, 'CRC sidecars do not apply to the service API');
assert.equal(model.photoWorkCount, 0, 'a warm queue does not redownload cached payloads');
fields['epaper-estimator-new-photos'].value = '7';
model = context.estimatorModel();
assert.equal(model.photoWorkCount, 7, 'new queued content is charged as a cache miss');
fields['epaper-estimator-new-photos'].value = '0';
context.sourceMode.value = 'slot-carousel';
context.sdCacheEnabled.checked = false;
model = context.estimatorModel();
assert.equal(model.panelRefreshes, 0, 'CRC can skip unchanged direct URLs');
assert.equal(model.crcApplies, true, 'CRC applies to direct URLs');
NODE

if grep -R -nE '(name="epaper_(source_mode|service_|wake_|rotation|crc32_enabled|sd_cache_|overlay_|frontlight_)|cfg\.epaper_(source_mode|service_|wake_|rotation|crc32_enabled|sd_cache_|overlay_|frontlight_|carousel|schedule_))' \
    src/app/device_classes/epaper_frame/web; then
    echo "FAIL: retired E-Paper Frame portal config key found" >&2
    exit 1
fi

echo "PASS: E-Paper Frame portal configuration contract is aligned."