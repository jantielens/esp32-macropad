// portal_health_sparkline.js - Sparkline drawing, tooltips, and hover interaction
// Part of the ESP32 Macropad configuration portal.
// Bundled into portal_health.js during minification.

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

