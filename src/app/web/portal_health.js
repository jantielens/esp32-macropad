// portal_health.js - Health monitoring widget with sparklines
// Part of the ESP32 Macropad configuration portal.

// ===== HEALTH WIDGET =====

const API_HEALTH = '/api/health';
const API_HEALTH_HISTORY = '/api/health/history';

let healthExpanded = false;
let healthPollTimer = null;

// Most recent /api/health snapshot, updated by every successful poll inside
// updateHealth(). Other portal fragments (welcome, version-info) read this
// instead of issuing their own /api/health requests — see fetchHealthOnce().
let latestHealth = null;
let healthFetchInflight = null;

/**
 * Synchronous accessor for the most recent /api/health snapshot.
 * Returns null if the health widget has never completed a poll.
 */
function getLatestHealth() {
    return latestHealth;
}

/**
 * Returns a Promise resolving to the most recent /api/health snapshot.
 * If the widget has already polled, resolves immediately from `latestHealth`.
 * Otherwise issues a single coalesced fetch (concurrent callers share the
 * in-flight Promise) so welcome/version-info fragments do not each spawn
 * their own /api/health request during portal boot.
 */
function fetchHealthOnce() {
    if (latestHealth) return Promise.resolve(latestHealth);
    if (healthFetchInflight) return healthFetchInflight;
    healthFetchInflight = fetch(API_HEALTH)
        .then(function (r) { return r.ok ? r.json() : null; })
        .then(function (h) {
            healthFetchInflight = null;
            if (h) latestHealth = h;
            return h;
        })
        .catch(function (err) {
            healthFetchInflight = null;
            console.error('fetchHealthOnce:', err);
            return null;
        });
    return healthFetchInflight;
}

const HEALTH_POLL_INTERVAL_DEFAULT_MS = 5000;
const HEALTH_HISTORY_DEFAULT_SECONDS = 300;
let healthPollIntervalMs = HEALTH_POLL_INTERVAL_DEFAULT_MS;
let healthHistoryMaxSamples = 60;

let healthDeviceHistoryAvailable = false;
let healthDeviceHistoryPeriodMs = HEALTH_POLL_INTERVAL_DEFAULT_MS;
let healthLastHistoryFetchMs = 0;

const healthHistory = {
    cpu: [],
    cpuTs: [],
    heapInternalFree: [],
    heapInternalFreeTs: [],
    heapInternalFreeMin: [],
    heapInternalFreeMax: [],
    psramFree: [],
    psramFreeTs: [],
    psramFreeMin: [],
    psramFreeMax: [],
    heapInternalLargest: [],
    heapInternalLargestTs: [],
};

const healthSeriesStats = {
    cpu: { min: null, max: null },
    heapInternalFree: { min: null, max: null },
    psramFree: { min: null, max: null },
    heapInternalLargest: { min: null, max: null },
};

function healthComputeMinMaxMulti(arrays) {
    const list = Array.isArray(arrays) ? arrays : [];
    let min = Infinity;
    let max = -Infinity;
    let seen = false;

    for (let k = 0; k < list.length; k++) {
        const arr = list[k];
        if (!Array.isArray(arr) || arr.length < 1) continue;
        for (let i = 0; i < arr.length; i++) {
            const v = arr[i];
            if (typeof v !== 'number' || !isFinite(v)) continue;
            seen = true;
            if (v < min) min = v;
            if (v > max) max = v;
        }
    }

    if (!seen || !isFinite(min) || !isFinite(max)) return { min: null, max: null };
    return { min, max };
}

function healthUpdateSeriesStats({ hasPsram = null } = {}) {
    const resolvedHasPsram = (typeof hasPsram === 'boolean') ? hasPsram : (healthHistory.psramFree && healthHistory.psramFree.length > 0);
    {
        const mm = healthComputeMinMaxMulti([healthHistory.cpu]);
        healthSeriesStats.cpu.min = mm.min;
        healthSeriesStats.cpu.max = mm.max;
    }
    {
        const mm = healthComputeMinMaxMulti([
            healthHistory.heapInternalFree,
            healthHistory.heapInternalFreeMin,
            healthHistory.heapInternalFreeMax,
        ]);
        healthSeriesStats.heapInternalFree.min = mm.min;
        healthSeriesStats.heapInternalFree.max = mm.max;
    }
    if (resolvedHasPsram) {
        const mm = healthComputeMinMaxMulti([
            healthHistory.psramFree,
            healthHistory.psramFreeMin,
            healthHistory.psramFreeMax,
        ]);
        healthSeriesStats.psramFree.min = mm.min;
        healthSeriesStats.psramFree.max = mm.max;
    } else {
        healthSeriesStats.psramFree.min = null;
        healthSeriesStats.psramFree.max = null;
    }
    {
        const mm = healthComputeMinMaxMulti([
            healthHistory.heapInternalLargest,
        ]);
        healthSeriesStats.heapInternalLargest.min = mm.min;
        healthSeriesStats.heapInternalLargest.max = mm.max;
    }
}

function healthConfigureFromDeviceInfo(info) {
    const pollMs = (info && typeof info.health_poll_interval_ms === 'number') ? info.health_poll_interval_ms : HEALTH_POLL_INTERVAL_DEFAULT_MS;
    const windowSeconds = (info && typeof info.health_history_seconds === 'number') ? info.health_history_seconds : HEALTH_HISTORY_DEFAULT_SECONDS;

    healthPollIntervalMs = Math.max(1000, Math.min(60000, Math.trunc(pollMs)));
    const seconds = Math.max(30, Math.min(3600, Math.trunc(windowSeconds)));
    healthHistoryMaxSamples = Math.max(10, Math.min(600, Math.floor((seconds * 1000) / healthPollIntervalMs)));
}

function healthConfigureHistoryFromDeviceInfo(info) {
    healthDeviceHistoryAvailable = (info && info.health_history_available === true);
    const p = (info && typeof info.health_history_period_ms === 'number') ? info.health_history_period_ms : null;
    healthDeviceHistoryPeriodMs = (typeof p === 'number' && isFinite(p) && p > 0) ? Math.trunc(p) : healthPollIntervalMs;

    const pointsWrap = document.getElementById('health-points-wrap');
    const sparklinesWrap = document.getElementById('health-sparklines-wrap');
    if (pointsWrap) pointsWrap.style.display = healthDeviceHistoryAvailable ? 'none' : '';
    if (sparklinesWrap) sparklinesWrap.style.display = healthDeviceHistoryAvailable ? '' : 'none';
}

function healthMakeSyntheticTs(count, periodMs) {
    const n = (typeof count === 'number' && isFinite(count)) ? Math.max(0, Math.trunc(count)) : 0;
    const p = (typeof periodMs === 'number' && isFinite(periodMs)) ? Math.max(1, Math.trunc(periodMs)) : HEALTH_POLL_INTERVAL_DEFAULT_MS;
    const now = Date.now();
    const out = new Array(n);
    for (let i = 0; i < n; i++) {
        // Oldest sample first.
        const age = (n - 1 - i) * p;
        out[i] = now - age;
    }
    return out;
}

function healthReplaceArray(dst, src) {
    if (!Array.isArray(dst)) return;
    dst.length = 0;
    if (Array.isArray(src)) {
        for (let i = 0; i < src.length; i++) dst.push(src[i]);
    }
}

async function updateHealthHistory({ hasPsram = null } = {}) {
    if (!healthDeviceHistoryAvailable) return;
    if (!healthExpanded) return;

    const now = Date.now();
    const minInterval = Math.max(1500, healthDeviceHistoryPeriodMs);
    if (now - healthLastHistoryFetchMs < minInterval) return;
    healthLastHistoryFetchMs = now;

    try {
        const resp = await fetch(API_HEALTH_HISTORY);
        if (!resp.ok) return;
        const hist = await resp.json();
        if (!hist || hist.available !== true) return;

        const periodMs = (typeof hist.period_ms === 'number' && isFinite(hist.period_ms) && hist.period_ms > 0) ? Math.trunc(hist.period_ms) : healthDeviceHistoryPeriodMs;
        const ts = healthMakeSyntheticTs(Array.isArray(hist.cpu_usage) ? hist.cpu_usage.length : 0, periodMs);

        healthReplaceArray(healthHistory.cpu, hist.cpu_usage);
        healthReplaceArray(healthHistory.cpuTs, ts);

        healthReplaceArray(healthHistory.heapInternalFree, hist.heap_internal_free);
        healthReplaceArray(healthHistory.heapInternalFreeTs, ts);
        healthReplaceArray(healthHistory.heapInternalFreeMin, hist.heap_internal_free_min_window);
        healthReplaceArray(healthHistory.heapInternalFreeMax, hist.heap_internal_free_max_window);

        healthReplaceArray(healthHistory.psramFree, hist.psram_free);
        healthReplaceArray(healthHistory.psramFreeTs, ts);
        healthReplaceArray(healthHistory.psramFreeMin, hist.psram_free_min_window);
        healthReplaceArray(healthHistory.psramFreeMax, hist.psram_free_max_window);

        healthReplaceArray(healthHistory.heapInternalLargest, hist.heap_internal_largest);
        healthReplaceArray(healthHistory.heapInternalLargestTs, ts);

        healthUpdateSeriesStats({ hasPsram });
        healthDrawSparklinesOnly({ hasPsram });
    } catch (e) {
        console.error('Failed to fetch /api/health/history:', e);
    }
}

function formatUptime(seconds) {
    const days = Math.floor(seconds / 86400);
    const hours = Math.floor((seconds % 86400) / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);
    const secs = Math.floor(seconds % 60);

    if (days > 0) return `${days}d ${hours}h ${minutes}m`;
    if (hours > 0) return `${hours}h ${minutes}m ${secs}s`;
    if (minutes > 0) return `${minutes}m ${secs}s`;
    return `${secs}s`;
}

function formatBytes(bytes) {
    if (bytes === null || bytes === undefined) return '--';
    const b = Number(bytes);
    if (!Number.isFinite(b)) return '--';

    if (b >= 1024 * 1024) return `${(b / (1024 * 1024)).toFixed(2)} MB`;
    if (b >= 1024) return `${(b / 1024).toFixed(1)} KB`;
    return `${Math.round(b)} B`;
}

function getSignalStrength(rssi) {
    if (rssi >= -50) return 'Excellent';
    if (rssi >= -60) return 'Good';
    if (rssi >= -70) return 'Fair';
    if (rssi >= -80) return 'Weak';
    return 'Very Weak';
}

function healthPushSample(arr, value) {
    if (!Array.isArray(arr)) return;
    if (typeof value !== 'number' || !isFinite(value)) return;
    arr.push(value);
    while (arr.length > healthHistoryMaxSamples) arr.shift();
}

function healthPushSampleWithTs(valuesArr, tsArr, value, ts) {
    if (!Array.isArray(valuesArr) || !Array.isArray(tsArr)) return;
    if (typeof value !== 'number' || !isFinite(value)) return;
    if (typeof ts !== 'number' || !isFinite(ts)) return;
    valuesArr.push(value);
    tsArr.push(ts);
    while (valuesArr.length > healthHistoryMaxSamples) valuesArr.shift();
    while (tsArr.length > healthHistoryMaxSamples) tsArr.shift();
}

function healthFormatAgeMs(ageMs) {
    if (typeof ageMs !== 'number' || !isFinite(ageMs)) return '';
    const s = Math.max(0, Math.round(ageMs / 1000));
    if (s < 60) return `${s}s`;
    const m = Math.floor(s / 60);
    const r = s % 60;
    if (m < 60) return `${m}m ${r}s`;
    const h = Math.floor(m / 60);
    const rm = m % 60;
    return `${h}h ${rm}m`;
}

function healthFormatTimeOfDay(ts) {
    try {
        return new Date(ts).toLocaleTimeString([], { hour12: false });
    } catch (_) {
        return '';
    }
}

function renderHealth(health) {
    // Compact badge
    const cpuBadge = document.getElementById('health-cpu');
    if (cpuBadge) {
        cpuBadge.textContent = (health.cpu_usage === null) ? 'CPU --' : `CPU ${health.cpu_usage}%`;
    }

    // Trigger breathing animation on status dots
    const dot = document.getElementById('health-status-dot');
    if (dot) {
        dot.classList.remove('breathing');
        void dot.offsetWidth;
        dot.classList.add('breathing');
    }
    const dotExpanded = document.getElementById('health-status-dot-expanded');
    if (dotExpanded) {
        dotExpanded.classList.remove('breathing');
        void dotExpanded.offsetWidth;
        dotExpanded.classList.add('breathing');
    }

    // System
    const uptimeEl = document.getElementById('health-uptime');
    if (uptimeEl) uptimeEl.textContent = formatUptime(health.uptime_seconds);
    const resetEl = document.getElementById('health-reset');
    if (resetEl) resetEl.textContent = health.reset_reason || 'Unknown';

    // CPU
    const cpuEl = document.getElementById('health-cpu-full');
    if (cpuEl) cpuEl.textContent = (health.cpu_usage === null) ? '—' : `${health.cpu_usage}%`;
    const tempEl = document.getElementById('health-temp');
    if (tempEl) tempEl.textContent = (health.cpu_temperature !== null) ? `${health.cpu_temperature}°C` : 'N/A';

    // Memory
    const heapFragEl = document.getElementById('health-heap-frag');
    if (heapFragEl) {
        if (typeof health.heap_fragmentation_max_window === 'number') {
            heapFragEl.textContent = `${health.heap_fragmentation}% (max ${health.heap_fragmentation_max_window}%)`;
        } else {
            heapFragEl.textContent = `${health.heap_fragmentation}%`;
        }
    }

    const internalMinEl = document.getElementById('health-internal-min');
    if (internalMinEl) internalMinEl.textContent = healthFormatBytes(health.heap_internal_min);

    const internalLargestEl = document.getElementById('health-internal-largest');
    if (internalLargestEl) {
        internalLargestEl.textContent = healthFormatBytes(health.heap_internal_largest);
    }

    const hasPsram = (
        (deviceInfoCache && typeof deviceInfoCache.psram_size === 'number' && deviceInfoCache.psram_size > 0) ||
        (typeof health.psram_free === 'number' && health.psram_free > 0)
    );

    const psramMinWrap = document.getElementById('health-psram-min-wrap');
    if (psramMinWrap) psramMinWrap.style.display = hasPsram ? '' : 'none';
    const psramMinEl = document.getElementById('health-psram-min');
    if (psramMinEl) psramMinEl.textContent = hasPsram ? healthFormatBytes(health.psram_min) : '—';

    // Flash
    const flashEl = document.getElementById('health-flash');
    if (flashEl) {
        flashEl.textContent = `${formatBytes(health.flash_used)} / ${formatBytes(health.flash_total)}`;
    }

    // Filesystem
    const fsEl = document.getElementById('health-fs');
    if (fsEl) {
        if (health.fs_mounted === null) {
            fsEl.textContent = 'Not present';
        } else if (!health.fs_mounted) {
            fsEl.textContent = 'Not mounted';
        } else if (health.fs_used_bytes !== null && health.fs_total_bytes !== null) {
            fsEl.textContent = `Storage ${formatBytes(health.fs_used_bytes)} / ${formatBytes(health.fs_total_bytes)}`;
        } else {
            fsEl.textContent = 'Storage mounted';
        }
    }

    // Network
    const ipEl = document.getElementById('health-ip');
    const rssiEl = document.getElementById('health-rssi');

    if (health.wifi_rssi !== null) {
        const strength = getSignalStrength(health.wifi_rssi);
        if (rssiEl) rssiEl.textContent = `${health.wifi_rssi} dBm (${strength})`;
        if (ipEl) ipEl.textContent = health.ip_address || 'N/A';
    } else {
        if (rssiEl) rssiEl.textContent = 'Not connected';
        if (ipEl) ipEl.textContent = 'N/A';
    }

    // MQTT
    const mqttEl = document.getElementById('health-mqtt');
    if (mqttEl) {
        if (!health.mqtt_enabled) {
            mqttEl.textContent = 'Disabled';
        } else {
            const status = health.mqtt_connected ? 'Connected' : 'Disconnected';
            const pub = health.mqtt_publish_enabled ? 'publish on' : 'publish off';
            const age = (health.mqtt_health_publish_age_ms === null) ? 'age --' : `age ${(health.mqtt_health_publish_age_ms / 1000).toFixed(0)}s`;
            mqttEl.textContent = `${status} (${pub}, ${age})`;
        }
    }

    // Display
    const displayRow = document.getElementById('health-display-row');
    const displayEl = document.getElementById('health-display');
    if (displayRow && displayEl) {
        const hasDisplay = deviceInfoCache && deviceInfoCache.has_display;
        displayRow.style.display = hasDisplay ? 'flex' : 'none';
        if (hasDisplay) {
            if (health.display_fps === null || health.display_fps === undefined) {
                displayEl.textContent = 'N/A';
            } else {
                displayEl.textContent = `${health.display_fps} fps`;
            }
        }
    }
}

function renderSensorsSection(health) {
    const section = document.getElementById('sensors-section');
    const badges = document.getElementById('sensor-badges');
    if (!section || !badges) return;

    const sensors = health && typeof health === 'object' ? health.sensors : null;

    const keys = sensors ? Object.keys(sensors) : [];
    if (keys.length === 0) {
        section.style.display = 'none';
        return;
    }

    section.style.display = 'block';
    badges.innerHTML = '';

    keys.sort();
    keys.forEach(key => {
        const value = sensors[key];
        const display = (value === undefined) ? '—' : JSON.stringify(value);
        const badge = document.createElement('span');
        badge.className = 'sensor-badge';
        badge.textContent = `${key}=${display}`;
        badges.appendChild(badge);
    });
}

function renderBleSection(health) {
    const section = document.getElementById('ble-section');
    if (!section) return;

    // Update BLE status only when health payload contains BLE data
    const hasBle = (typeof health.ble_status === 'string') || (typeof health.ble_state === 'string');
    if (!hasBle) return;

    const dot = document.getElementById('ble-status-dot');
    const text = document.getElementById('ble-status-text');
    const nameWrap = document.getElementById('ble-name-wrap');
    const nameEl = document.getElementById('ble-name');
    const details = document.getElementById('ble-details');
    const pairBtn = document.getElementById('ble-pair-btn');

    const bleStatus = health.ble_status || 'ready';
    const isPairing = bleStatus === 'pairing';
    const isConnected = bleStatus === 'connected';
    const isDisabled = bleStatus === 'disabled';
    const isError = bleStatus === 'error';

    // Status dot color
    if (dot) {
        if (isPairing) dot.style.background = '#ff9500';
        else if (isConnected) dot.style.background = '#34c759';
        else if (isError) dot.style.background = '#ff3b30';
        else if (bleStatus === 'ready') dot.style.background = '#0a84ff';
        else dot.style.background = '#ccc';
    }

    // Status text
    if (text) {
        if (isPairing) text.textContent = 'Pairing mode (waiting for device\u2026)';
        else if (isConnected) text.textContent = 'Connected';
        else if (bleStatus === 'ready') text.textContent = 'Ready';
        else if (isDisabled) text.textContent = 'Disabled';
        else if (isError) text.textContent = 'Error';
        else text.textContent = 'Ready';
    }

    if (nameWrap && nameEl) {
        const bleName = health.ble_name || '';
        nameWrap.style.display = bleName ? '' : 'none';
        nameEl.textContent = bleName;
    }

    // Details panel (only when connected)
    if (details) details.style.display = isConnected ? 'block' : 'none';

    if (isConnected) {
        const bondBadge = document.getElementById('ble-badge-bonded');
        if (bondBadge) {
            bondBadge.style.display = '';
            bondBadge.textContent = health.ble_bonded ? 'Bonded' : 'Not bonded';
        }
        const encBadge = document.getElementById('ble-badge-encrypted');
        if (encBadge) {
            encBadge.style.display = '';
            encBadge.textContent = health.ble_encrypted ? 'Encrypted' : 'Not encrypted';
        }
        const peerWrap = document.getElementById('ble-peer-addr-wrap');
        const peerEl = document.getElementById('ble-peer-addr');
        if (peerWrap && peerEl) {
            const addr = health.ble_peer_addr || '';
            peerWrap.style.display = addr ? '' : 'none';
            peerEl.textContent = addr;
        }
        const idWrap = document.getElementById('ble-id-addr-wrap');
        const idEl = document.getElementById('ble-peer-id-addr');
        if (idWrap && idEl) {
            const addr = health.ble_peer_id_addr || '';
            idWrap.style.display = addr ? '' : 'none';
            idEl.textContent = addr;
        }
    }

    // Disable pair button while pairing
    if (pairBtn) {
        pairBtn.disabled = isPairing || isDisabled;
        pairBtn.textContent = isPairing ? 'Pairing\u2026' : 'Pair New Device';
    }
}

function toggleBleContent() {
    const cb = document.getElementById('ble_enabled');
    const content = document.getElementById('ble-content');
    if (cb && content) content.style.display = cb.checked ? 'block' : 'none';
}

async function startBlePairing() {
    const btn = document.getElementById('ble-pair-btn');
    if (btn) { btn.disabled = true; btn.textContent = 'Starting\u2026'; }
    try {
        const resp = await fetch('/api/ble/pairing/start', { method: 'POST' });
        if (!resp.ok) {
            alert('Failed to start pairing: ' + resp.status);
        }
    } catch (e) {
        alert('Failed to start pairing: ' + e.message);
    }
    // Next health poll will update the UI
}

async function updateHealth() {
    try {
        const response = await fetch(API_HEALTH);
        if (!response.ok) return;

        const health = await response.json();
        // Publish snapshot for fetchHealthOnce() consumers (welcome + version
        // fragments) so they don't issue their own /api/health requests.
        latestHealth = health;

        const cpuUsage = (typeof health.cpu_usage === 'number' && isFinite(health.cpu_usage)) ? Math.floor(health.cpu_usage) : null;
        const hasPsram = (
            (deviceInfoCache && typeof deviceInfoCache.psram_size === 'number' && deviceInfoCache.psram_size > 0) ||
            (typeof health.psram_free === 'number' && health.psram_free > 0)
        );

        // Update point-in-time rows (shown when history is unavailable).
        const ptCpu = document.getElementById('health-point-cpu-value');
        if (ptCpu) ptCpu.textContent = (cpuUsage !== null) ? `${cpuUsage}%` : '—';
        const ptHeap = document.getElementById('health-point-heap-value');
        if (ptHeap) ptHeap.textContent = healthFormatBytes(health.heap_internal_free);
        const ptPsramWrap = document.getElementById('health-point-psram-wrap');
        if (ptPsramWrap) ptPsramWrap.style.display = hasPsram ? '' : 'none';
        const ptPsram = document.getElementById('health-point-psram-value');
        if (ptPsram) ptPsram.textContent = hasPsram ? healthFormatBytes(health.psram_free) : '—';
        const ptLargest = document.getElementById('health-point-largest-value');
        if (ptLargest) ptLargest.textContent = healthFormatBytes(health.heap_internal_largest);

        // Update sparkline header values.
        const cpuSparkValue = document.getElementById('health-sparkline-cpu-value');
        if (cpuSparkValue) cpuSparkValue.textContent = (cpuUsage !== null) ? `${cpuUsage}%` : '—';

        const heapSparkValue = document.getElementById('health-sparkline-heap-value');
        if (heapSparkValue) heapSparkValue.textContent = healthFormatBytes(health.heap_internal_free);

        const psramWrap = document.getElementById('health-sparkline-psram-wrap');
        if (psramWrap) psramWrap.style.display = hasPsram ? '' : 'none';
        const psramSparkValue = document.getElementById('health-sparkline-psram-value');
        if (psramSparkValue) psramSparkValue.textContent = hasPsram ? healthFormatBytes(health.psram_free) : '—';

        const largestSparkValue = document.getElementById('health-sparkline-largest-value');
        if (largestSparkValue) largestSparkValue.textContent = healthFormatBytes(health.heap_internal_largest);

        renderHealth(health);
        renderSensorsSection(health);
        renderBleSection(health);
        if (healthExpanded) {
            await updateHealthHistory({ hasPsram });
        }
    } catch (error) {
        console.error('Failed to fetch health stats:', error);
    }
}

function toggleHealthWidget() {
    healthExpanded = !healthExpanded;
    const expandedEl = document.getElementById('health-expanded');
    if (!expandedEl) return;

    expandedEl.style.display = healthExpanded ? 'block' : 'none';
    if (healthExpanded) {
        updateHealth();
        updateHealthHistory({
            hasPsram: (() => {
                const wrap = document.getElementById('health-sparkline-psram-wrap');
                return wrap ? (wrap.style.display !== 'none') : null;
            })(),
        });
    } else {
        healthTooltipSetVisible(false);
    }
}

let _healthWidgetWired = false;
function initHealthWidget() {
    // Idempotent: badge + close button live in the shell (not a fragment),
    // so we only ever wire their click handlers once. Without this guard,
    // re-entering pad-editor or sensor-data would attach duplicate listeners
    // and an even number of toggles would cancel out (close button "broken").
    if (!_healthWidgetWired) {
        const healthBadge = document.getElementById('health-badge');
        if (healthBadge) {
            healthBadge.addEventListener('click', toggleHealthWidget);
        }
        const closeBtn = document.getElementById('health-close');
        if (closeBtn) {
            closeBtn.addEventListener('click', toggleHealthWidget);
        }
        _healthWidgetWired = true;
    }

    // Configure polling based on device info if available.
    healthConfigureFromDeviceInfo(deviceInfoCache);
    healthConfigureHistoryFromDeviceInfo(deviceInfoCache);

    // Attach hover/touch tooltips once.
    healthInitSparklineTooltips();

    // Start polling. loadVersion() fills deviceInfoCache asynchronously; we tune interval after first info fetch.
    const startPolling = () => {
        if (healthPollTimer) {
            clearInterval(healthPollTimer);
            healthPollTimer = null;
        }
        healthConfigureFromDeviceInfo(deviceInfoCache);
        healthConfigureHistoryFromDeviceInfo(deviceInfoCache);
        healthPollTimer = setInterval(updateHealth, healthPollIntervalMs);
    };

    // Initial
    updateHealth();
    startPolling();

    // Re-tune polling once deviceInfoCache becomes available.
    setTimeout(startPolling, 1500);
}
