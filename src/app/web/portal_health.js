// portal_health.js - Health monitoring widget with sparklines
// Part of the ESP32 Macropad configuration portal.

// ===== HEALTH WIDGET =====

const API_HEALTH = '/api/health';
const API_HEALTH_HISTORY = '/api/health/history';

let healthExpanded = false;
let healthPollTimer = null;

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

let healthSparklineTooltipEl = null;
function healthEnsureSparklineTooltip() {
    if (healthSparklineTooltipEl) return healthSparklineTooltipEl;
    const el = document.createElement('div');
    el.className = 'health-sparkline-tooltip';
    el.style.display = 'none';
    document.body.appendChild(el);
    healthSparklineTooltipEl = el;
    return el;
}

function healthTooltipSetVisible(visible) {
    const el = healthEnsureSparklineTooltip();
    el.style.display = visible ? 'block' : 'none';
}

function healthTooltipSetContent(html) {
    const el = healthEnsureSparklineTooltip();
    el.innerHTML = html;
}

function healthTooltipSetPosition(clientX, clientY) {
    const el = healthEnsureSparklineTooltip();

    const pad = 12;
    let x = (clientX || 0) + pad;
    let y = (clientY || 0) + pad;

    const vw = window.innerWidth || 0;
    const vh = window.innerHeight || 0;

    const maxW = (vw > 0) ? Math.max(140, vw - pad * 2) : 320;
    const desiredW = 280;
    el.style.width = `${Math.min(desiredW, maxW)}px`;
    el.style.maxWidth = `${maxW}px`;

    const prevDisplay = el.style.display;
    el.style.display = 'block';
    const rect = el.getBoundingClientRect();
    el.style.display = prevDisplay;

    if (vw > 0 && rect.width > 0 && x + rect.width + pad > vw) {
        x = Math.max(pad, vw - rect.width - pad);
    }
    if (vh > 0 && rect.height > 0 && y + rect.height + pad > vh) {
        y = Math.max(pad, vh - rect.height - pad);
    }

    el.style.left = `${x}px`;
    el.style.top = `${y}px`;
}

function healthSparklineIndexFromEvent(canvas, clientX) {
    if (!canvas) return null;
    const rect = canvas.getBoundingClientRect();
    const w = rect.width || 0;
    if (w <= 0) return null;
    const x = (clientX - rect.left);
    const t = Math.max(0, Math.min(1, x / w));
    return t;
}

const healthSparklineHoverIndex = {
    'health-sparkline-cpu': null,
    'health-sparkline-heap': null,
    'health-sparkline-psram': null,
    'health-sparkline-largest': null,
};

function healthSetSparklineHoverIndex(canvasId, index) {
    if (!canvasId) return;
    if (!(canvasId in healthSparklineHoverIndex)) return;
    if (typeof index !== 'number' || !isFinite(index)) {
        healthSparklineHoverIndex[canvasId] = null;
        return;
    }
    healthSparklineHoverIndex[canvasId] = Math.trunc(index);
}

function healthGetSparklineHoverIndex(canvasId) {
    if (!canvasId) return null;
    if (!(canvasId in healthSparklineHoverIndex)) return null;
    const v = healthSparklineHoverIndex[canvasId];
    return (typeof v === 'number' && isFinite(v)) ? Math.trunc(v) : null;
}

function healthFormatBytes(bytes) {
    if (typeof bytes !== 'number' || !isFinite(bytes)) return '—';
    return formatBytes(bytes);
}

function healthFormatBytesKB(bytes) {
    if (typeof bytes !== 'number' || !isFinite(bytes)) return '—';
    const kb = bytes / 1024;
    const decimals = (kb >= 1000) ? 0 : 1;
    return `${kb.toFixed(decimals)} KB`;
}

function sparklineDraw(canvas, values, {
    color = '#667eea',
    strokeWidth = 2,
    min = null,
    max = null,
    bandMin = null,
    bandMax = null,
    bandColor = 'rgba(102, 126, 234, 0.18)',
    highlightIndex = null,
    highlightRadius = 3.25,
    highlightFill = 'rgba(255,255,255,0.95)',
    highlightStroke = null,
    highlightStrokeWidth = 2,
} = {}) {
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const w = canvas.width;
    const h = canvas.height;
    ctx.clearRect(0, 0, w, h);

    const data = Array.isArray(values) ? values : [];
    if (data.length < 1) {
        ctx.strokeStyle = 'rgba(0,0,0,0.08)';
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(0, h - 1);
        ctx.lineTo(w, h - 1);
        ctx.stroke();
        return;
    }

    const bandMinArr = Array.isArray(bandMin) ? bandMin : null;
    const bandMaxArr = Array.isArray(bandMax) ? bandMax : null;

    let vmin = (typeof min === 'number') ? min : Infinity;
    let vmax = (typeof max === 'number') ? max : -Infinity;
    if (!(typeof min === 'number') || !(typeof max === 'number')) {
        for (let i = 0; i < data.length; i++) {
            const v = data[i];
            if (typeof v === 'number' && isFinite(v)) {
                if (v < vmin) vmin = v;
                if (v > vmax) vmax = v;
            }
            if (bandMinArr && i < bandMinArr.length) {
                const bmin = bandMinArr[i];
                if (typeof bmin === 'number' && isFinite(bmin)) {
                    if (bmin < vmin) vmin = bmin;
                    if (bmin > vmax) vmax = bmin;
                }
            }
            if (bandMaxArr && i < bandMaxArr.length) {
                const bmax = bandMaxArr[i];
                if (typeof bmax === 'number' && isFinite(bmax)) {
                    if (bmax < vmin) vmin = bmax;
                    if (bmax > vmax) vmax = bmax;
                }
            }
        }
    }
    if (!isFinite(vmin) || !isFinite(vmax)) {
        vmin = 0;
        vmax = 1;
    } else if (vmin === vmax) {
        const eps = Math.max(1, Math.abs(vmin) * 0.01);
        vmin = vmin - eps;
        vmax = vmax + eps;
    }

    const pad = 4;
    const xStep = (data.length >= 2) ? ((w - pad * 2) / (data.length - 1)) : 0;
    const yScale = (h - pad * 2) / (vmax - vmin);

    ctx.strokeStyle = 'rgba(0,0,0,0.06)';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(0, h - 1);
    ctx.lineTo(w, h - 1);
    ctx.stroke();

    if (bandMinArr && bandMaxArr && data.length >= 2) {
        const n = Math.min(data.length, bandMinArr.length, bandMaxArr.length);
        if (n >= 2) {
            ctx.fillStyle = bandColor;
            ctx.beginPath();
            for (let i = 0; i < n; i++) {
                const bmax = bandMaxArr[i];
                if (typeof bmax !== 'number' || !isFinite(bmax)) continue;
                const x = pad + i * xStep;
                const y = h - pad - ((bmax - vmin) * yScale);
                if (i === 0) ctx.moveTo(x, y);
                else ctx.lineTo(x, y);
            }
            for (let i = n - 1; i >= 0; i--) {
                const bmin = bandMinArr[i];
                if (typeof bmin !== 'number' || !isFinite(bmin)) continue;
                const x = pad + i * xStep;
                const y = h - pad - ((bmin - vmin) * yScale);
                ctx.lineTo(x, y);
            }
            ctx.closePath();
            ctx.fill();
        }
    }

    ctx.strokeStyle = color;
    ctx.lineWidth = strokeWidth;
    ctx.lineJoin = 'round';
    ctx.lineCap = 'round';

    if (data.length === 1) {
        const v = data[0];
        const x = pad;
        const y = h - pad - ((v - vmin) * yScale);
        ctx.beginPath();
        ctx.arc(x, y, 2.5, 0, Math.PI * 2);
        ctx.stroke();
        return;
    }

    ctx.beginPath();
    for (let i = 0; i < data.length; i++) {
        const v = data[i];
        const x = pad + i * xStep;
        const y = h - pad - ((v - vmin) * yScale);
        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
    }
    ctx.stroke();

    if (typeof highlightIndex === 'number' && isFinite(highlightIndex)) {
        const idx = Math.max(0, Math.min(data.length - 1, Math.trunc(highlightIndex)));
        const v = data[idx];
        if (typeof v === 'number' && isFinite(v)) {
            const x = pad + idx * xStep;
            const y = h - pad - ((v - vmin) * yScale);
            const strokeCol = highlightStroke || color;
            const r = Math.max(2.0, highlightRadius);

            ctx.fillStyle = highlightFill;
            ctx.beginPath();
            ctx.arc(x, y, r, 0, Math.PI * 2);
            ctx.fill();

            ctx.strokeStyle = strokeCol;
            ctx.lineWidth = highlightStrokeWidth;
            ctx.beginPath();
            ctx.arc(x, y, r, 0, Math.PI * 2);
            ctx.stroke();
        }
    }
}

function healthDrawSparklinesOnly({ hasPsram = null } = {}) {
    const resolvedHasPsram = (typeof hasPsram === 'boolean') ? hasPsram : (healthHistory.psramFree && healthHistory.psramFree.length > 0);

    sparklineDraw(document.getElementById('health-sparkline-cpu'), healthHistory.cpu, {
        color: '#667eea',
        min: 0,
        max: 100,
        highlightIndex: healthGetSparklineHoverIndex('health-sparkline-cpu'),
    });

    sparklineDraw(document.getElementById('health-sparkline-heap'), healthHistory.heapInternalFree, {
        color: '#34c759',
        bandMin: healthHistory.heapInternalFreeMin,
        bandMax: healthHistory.heapInternalFreeMax,
        bandColor: 'rgba(52, 199, 89, 0.18)',
        highlightIndex: healthGetSparklineHoverIndex('health-sparkline-heap'),
    });

    sparklineDraw(document.getElementById('health-sparkline-psram'), healthHistory.psramFree, {
        color: '#0a84ff',
        bandMin: healthHistory.psramFreeMin,
        bandMax: healthHistory.psramFreeMax,
        bandColor: 'rgba(10, 132, 255, 0.18)',
        highlightIndex: resolvedHasPsram ? healthGetSparklineHoverIndex('health-sparkline-psram') : null,
    });

    sparklineDraw(document.getElementById('health-sparkline-largest'), healthHistory.heapInternalLargest, {
        color: '#ff2d55',
        highlightIndex: healthGetSparklineHoverIndex('health-sparkline-largest'),
    });
}

function healthAttachSparklineTooltip(canvas, getPayloadForIndex) {
    if (!canvas || typeof getPayloadForIndex !== 'function') return;
    if (canvas.dataset && canvas.dataset.healthTooltipAttached === '1') return;
    if (canvas.dataset) canvas.dataset.healthTooltipAttached = '1';

    let hideTimer = null;
    const clearHideTimer = () => {
        if (hideTimer) {
            clearTimeout(hideTimer);
            hideTimer = null;
        }
    };

    const hide = () => {
        clearHideTimer();
        healthSetSparklineHoverIndex(canvas.id, null);
        healthDrawSparklinesOnly({
            hasPsram: (() => {
                const wrap = document.getElementById('health-sparkline-psram-wrap');
                return wrap ? (wrap.style.display !== 'none') : null;
            })(),
        });
        healthTooltipSetVisible(false);
    };

    const showAt = (clientX, clientY) => {
        clearHideTimer();
        const t = healthSparklineIndexFromEvent(canvas, clientX);
        if (t === null) return;

        const payload = getPayloadForIndex(t);
        if (!payload) return;

        if (typeof payload.index === 'number' && isFinite(payload.index)) {
            const prev = healthGetSparklineHoverIndex(canvas.id);
            const next = Math.trunc(payload.index);
            if (prev !== next) {
                healthSetSparklineHoverIndex(canvas.id, next);
                healthDrawSparklinesOnly({
                    hasPsram: (() => {
                        const wrap = document.getElementById('health-sparkline-psram-wrap');
                        return wrap ? (wrap.style.display !== 'none') : null;
                    })(),
                });
            }
        }

        healthTooltipSetContent(payload.html);
        healthTooltipSetPosition(clientX, clientY);
        healthTooltipSetVisible(true);
    };

    canvas.addEventListener('mousemove', (e) => {
        showAt(e.clientX, e.clientY);
    });
    canvas.addEventListener('mouseleave', hide);

    canvas.addEventListener('touchstart', (e) => {
        if (!e.touches || e.touches.length < 1) return;
        const t0 = e.touches[0];
        showAt(t0.clientX, t0.clientY);
    }, { passive: true });
    canvas.addEventListener('touchmove', (e) => {
        if (!e.touches || e.touches.length < 1) return;
        const t0 = e.touches[0];
        showAt(t0.clientX, t0.clientY);
    }, { passive: true });
    canvas.addEventListener('touchend', () => {
        clearHideTimer();
        hideTimer = setTimeout(hide, 1200);
    }, { passive: true });
}

function healthInitSparklineTooltips() {
    const formatMinMaxDeltaLine = (minVal, maxVal, fmt) => {
        if (typeof fmt !== 'function') {
            fmt = (v) => String(v);
        }
        if (typeof minVal !== 'number' || !isFinite(minVal) || typeof maxVal !== 'number' || !isFinite(maxVal)) {
            return 'min: —, max: —, <span class="health-sparkline-tooltip-delta">Δ —</span>';
        }
        const delta = Math.max(0, maxVal - minVal);
        return `min: ${fmt(minVal)}, max: ${fmt(maxVal)}, <span class="health-sparkline-tooltip-delta">Δ ${fmt(delta)}</span>`;
    };

    const tooltipHtml = ({ title, age, hero, windowLineHtml, sparklineLineHtml }) => {
        const win = windowLineHtml ? `<div class="health-sparkline-tooltip-line">${windowLineHtml}</div>` : '';
        return (
            `<div class="health-sparkline-tooltip-header">` +
                `<div class="health-sparkline-tooltip-title">${title || ''}</div>` +
                `<div class="health-sparkline-tooltip-age">${age || ''}</div>` +
            `</div>` +
            `<div class="health-sparkline-tooltip-hero">${hero || '—'}</div>` +
            win +
            `<div class="health-sparkline-tooltip-section">Sparkline window</div>` +
            `<div class="health-sparkline-tooltip-line">${sparklineLineHtml || 'min: —, max: —, <span class="health-sparkline-tooltip-delta">Δ —</span>'}</div>`
        );
    };

    const cpuCanvas = document.getElementById('health-sparkline-cpu');
    healthAttachSparklineTooltip(cpuCanvas, (t) => {
        const v = healthHistory.cpu;
        const ts = healthHistory.cpuTs;
        const n = v.length;
        if (n < 1) return null;
        const i = Math.max(0, Math.min(n - 1, Math.round(t * (n - 1))));
        const val = v[i];
        const tsv = ts[i];
        const age = healthFormatAgeMs(Date.now() - tsv);
        const smin = healthSeriesStats.cpu.min;
        const smax = healthSeriesStats.cpu.max;

        const sparklineLine = formatMinMaxDeltaLine(
            (typeof smin === 'number') ? smin : NaN,
            (typeof smax === 'number') ? smax : NaN,
            (x) => `${Math.trunc(x)}%`
        );

        return {
            index: i,
            html: tooltipHtml({
                title: 'CPU Usage',
                age,
                hero: (typeof val === 'number' && isFinite(val)) ? `${val}%` : '—',
                windowLineHtml: null,
                sparklineLineHtml: sparklineLine,
            }),
        };
    });

    const heapCanvas = document.getElementById('health-sparkline-heap');
    healthAttachSparklineTooltip(heapCanvas, (t) => {
        const v = healthHistory.heapInternalFree;
        const ts = healthHistory.heapInternalFreeTs;
        const bmin = healthHistory.heapInternalFreeMin;
        const bmax = healthHistory.heapInternalFreeMax;
        const n = v.length;
        if (n < 1) return null;
        const i = Math.max(0, Math.min(n - 1, Math.round(t * (n - 1))));
        const val = v[i];
        const tsv = ts[i];
        const wmin = (i < bmin.length) ? bmin[i] : val;
        const wmax = (i < bmax.length) ? bmax[i] : val;
        const age = healthFormatAgeMs(Date.now() - tsv);
        const smin = healthSeriesStats.heapInternalFree.min;
        const smax = healthSeriesStats.heapInternalFree.max;

        const windowLine = formatMinMaxDeltaLine(wmin, wmax, healthFormatBytes);
        const sparklineLine = formatMinMaxDeltaLine(smin, smax, healthFormatBytes);

        return {
            index: i,
            html: tooltipHtml({
                title: 'Internal Free Heap',
                age,
                hero: healthFormatBytes(val),
                windowLineHtml: windowLine,
                sparklineLineHtml: sparklineLine,
            }),
        };
    });

    const psramCanvas = document.getElementById('health-sparkline-psram');
    healthAttachSparklineTooltip(psramCanvas, (t) => {
        const v = healthHistory.psramFree;
        const ts = healthHistory.psramFreeTs;
        const bmin = healthHistory.psramFreeMin;
        const bmax = healthHistory.psramFreeMax;
        const n = v.length;
        if (n < 1) return null;
        const i = Math.max(0, Math.min(n - 1, Math.round(t * (n - 1))));
        const val = v[i];
        const tsv = ts[i];
        const wmin = (i < bmin.length) ? bmin[i] : val;
        const wmax = (i < bmax.length) ? bmax[i] : val;
        const age = healthFormatAgeMs(Date.now() - tsv);
        const smin = healthSeriesStats.psramFree.min;
        const smax = healthSeriesStats.psramFree.max;

        const windowLine = formatMinMaxDeltaLine(wmin, wmax, healthFormatBytesKB);
        const sparklineLine = formatMinMaxDeltaLine(smin, smax, healthFormatBytesKB);

        return {
            index: i,
            html: tooltipHtml({
                title: 'PSRAM Free',
                age,
                hero: healthFormatBytesKB(val),
                windowLineHtml: windowLine,
                sparklineLineHtml: sparklineLine,
            }),
        };
    });

    const largestCanvas = document.getElementById('health-sparkline-largest');
    healthAttachSparklineTooltip(largestCanvas, (t) => {
        const v = healthHistory.heapInternalLargest;
        const ts = healthHistory.heapInternalLargestTs;
        const n = v.length;
        if (n < 1) return null;
        const i = Math.max(0, Math.min(n - 1, Math.round(t * (n - 1))));
        const val = v[i];
        const tsv = ts[i];
        const age = healthFormatAgeMs(Date.now() - tsv);
        const smin = healthSeriesStats.heapInternalLargest.min;
        const smax = healthSeriesStats.heapInternalLargest.max;

        const sparklineLine = formatMinMaxDeltaLine(smin, smax, healthFormatBytes);

        return {
            index: i,
            html: tooltipHtml({
                title: 'Internal Largest Block',
                age,
                hero: healthFormatBytes(val),
                sparklineLineHtml: sparklineLine,
            }),
        };
    });
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
    if (btn) { btn.disabled = true; btn.textContent = 'Rebooting\u2026'; }
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

function initHealthWidget() {
    const healthBadge = document.getElementById('health-badge');
    if (healthBadge) {
        healthBadge.addEventListener('click', toggleHealthWidget);
    }
    const closeBtn = document.getElementById('health-close');
    if (closeBtn) {
        closeBtn.addEventListener('click', toggleHealthWidget);
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