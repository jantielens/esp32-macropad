// portal_brews.js - Brew log list, detail, charts, export/import
// Part of the ESP32 Macropad configuration portal.

const API_BREWS = '/api/brews';

let brewListData = null;      // cached list response
let brewDetailData = null;    // cached single brew (full)
let brewListScroll = 0;       // scroll position to restore on back
let weightChart = null;
let flowChart = null;
let comboChart = null;

// ============================================================================
// Formatting helpers
// ============================================================================

function brewFormatField(field) {
    const v = field.value;
    const u = field.unit || '';
    switch (field.format) {
        case 'text':
            return v;
        case 'datetime':
            if (!v || v === 0) return 'Unknown date';
            return new Date(v * 1000).toLocaleString(undefined, {
                month: 'short', day: 'numeric', year: 'numeric',
                hour: '2-digit', minute: '2-digit'
            });
        case 'duration': {
            const totalSec = Math.round(v / 1000);
            const m = Math.floor(totalSec / 60);
            const s = totalSec % 60;
            return m + ':' + String(s).padStart(2, '0');
        }
        case 'number':
            if (field.key === 'ratio')
                return '1:' + (typeof v === 'number' ? v.toFixed(1) : v);
            return (typeof v === 'number' ? v.toFixed(1) : v) + (u ? ' ' + u : '');
        default:
            return String(v);
    }
}

function brewFindField(fields, key) {
    return fields.find(f => f.key === key);
}

// ============================================================================
// Stats Banner
// ============================================================================

function brewUpdateStats(brews) {
    const countEl = document.getElementById('stat-count');
    const avgTimeEl = document.getElementById('stat-avg-time');
    const avgWeightEl = document.getElementById('stat-avg-weight');

    if (!brews || brews.length === 0) {
        countEl.textContent = '0';
        avgTimeEl.textContent = '--';
        avgWeightEl.textContent = '--';
        return;
    }

    countEl.textContent = brews.length;

    let totalDur = 0, totalWeight = 0;
    let durCount = 0, weightCount = 0;

    for (const brew of brews) {
        const dur = brewFindField(brew.fields, 'duration');
        const wt = brewFindField(brew.fields, 'water');
        if (dur) { totalDur += dur.value; durCount++; }
        if (wt) { totalWeight += wt.value; weightCount++; }
    }

    if (durCount > 0) {
        const avgMs = totalDur / durCount;
        const totalSec = Math.round(avgMs / 1000);
        const m = Math.floor(totalSec / 60);
        const s = totalSec % 60;
        avgTimeEl.textContent = m + ':' + String(s).padStart(2, '0');
    } else {
        avgTimeEl.textContent = '--';
    }

    avgWeightEl.textContent = weightCount > 0 ? (totalWeight / weightCount).toFixed(1) + 'g' : '--';
}

// ============================================================================
// Brew Cards (List View)
// ============================================================================

function brewRenderCards(brews) {
    const container = document.getElementById('brew-cards');
    const emptyEl = document.getElementById('brew-empty');

    if (!brews || brews.length === 0) {
        container.innerHTML = '';
        emptyEl.style.display = 'block';
        return;
    }

    emptyEl.style.display = 'none';
    container.innerHTML = '';

    brews.forEach((brew, idx) => {
        const name = brewFindField(brew.fields, 'template');
        const ti = brew.template_info;
        const displayName = (ti && ti.display_name) ? ti.display_name : (name ? name.value : 'Brew');
        const ts = brewFindField(brew.fields, 'ts');
        const dur = brewFindField(brew.fields, 'duration');
        const wt = brewFindField(brew.fields, 'water');

        const card = document.createElement('div');
        card.className = 'brew-card';
        card.style.animationDelay = (idx * 0.05) + 's';
        card.setAttribute('tabindex', '0');
        card.setAttribute('role', 'button');
        card.setAttribute('aria-label', `View brew: ${displayName}`);

        card.innerHTML = `
            <div class="brew-card-header">
                <span class="brew-card-name">☕ ${displayName}</span>
                <span class="brew-card-date">${ts ? brewFormatField(ts) : ''}</span>
            </div>
            <div class="brew-card-stats">
                <div class="brew-card-stat">
                    <span class="brew-card-stat-value">${dur ? brewFormatField(dur) : '--'}</span>
                    <span class="brew-card-stat-label">Duration</span>
                </div>
                <div class="brew-card-stat">
                    <span class="brew-card-stat-value">${wt ? brewFormatField(wt) : '--'}</span>
                    <span class="brew-card-stat-label">Water</span>
                </div>
            </div>
            <button type="button" class="brew-card-delete" aria-label="Delete brew" data-id="${brew.id}">🗑️</button>
        `;

        card.addEventListener('click', (e) => {
            if (e.target.closest('.brew-card-delete')) return;
            brewShowDetail(brew.id);
        });
        card.addEventListener('keydown', (e) => {
            if (e.key === 'Enter') brewShowDetail(brew.id);
        });

        const delBtn = card.querySelector('.brew-card-delete');
        delBtn.addEventListener('click', (e) => {
            e.stopPropagation();
            brewDeleteOne(brew.id);
        });

        container.appendChild(card);
    });
}

// ============================================================================
// List View — load & display
// ============================================================================

async function brewLoadList() {
    try {
        const resp = await fetch(API_BREWS);
        if (!resp.ok) throw new Error('Failed to load brews');
        brewListData = await resp.json();
        brewRenderCards(brewListData.brews);
        brewUpdateStats(brewListData.brews);
    } catch (err) {
        console.error('brewLoadList error:', err);
        document.getElementById('brew-cards').innerHTML =
            '<p style="color:#EF5350; text-align:center;">Failed to load brews</p>';
    }
}

// ============================================================================
// Series Analysis — swirl detection, flow capping, derived metrics
// ============================================================================

// Max physically possible pour-over flow (g/s). Anything above is a swirl/scale
// disturbance. Shared by spike detection and phase computation.
const BREW_FLOW_HARD_CEILING = 15;

// Minimum flow (g/s) to count as active pour.
const BREW_POUR_THRESHOLD = 0.5;

// Tolerance for comparing actual vs target values.
const BREW_FIELD_TARGET_TOLERANCE = 0.05;  // 5% for weight fields
const BREW_FLOW_TARGET_TOLERANCE  = 0.15;  // 15% for flow rates (inherently noisier)

// Analyse flow data: detect swirl spikes, compute a clean Y-axis ceiling,
// and produce a display-ready array with spikes replaced by NaN.
// Swirl = cup lifted off scale then returned — produces physically impossible
// flow rates (tens of g/s) that crush the chart scale.
//
// Returns { flowDisplay: number[], flowCeiling: number, spikeSet: Set<number> }
function brewAnalyseFlow(flow) {
    if (!flow || flow.length === 0)
        return { flowDisplay: [], flowCeiling: 10, spikeSet: new Set() };

    // Step 1: collect non-trivial absolute flow values, excluding obvious spikes
    const absVals = [];
    for (let i = 0; i < flow.length; i++) {
        const a = Math.abs(flow[i]);
        if (a > 0.2 && a <= BREW_FLOW_HARD_CEILING) absVals.push(a);
    }
    absVals.sort((a, b) => a - b);

    // Step 2: compute spike threshold from P90 of "real" flow
    //   P90 captures the top of real pouring range (~6-7 g/s for pour-over).
    //   Anything above 2× P90 is physically impossible → spike.
    //   Fallback: hard ceiling if very few samples.
    let spikeThreshold = BREW_FLOW_HARD_CEILING;
    if (absVals.length >= 5) {
        const p90 = absVals[Math.floor(absVals.length * 0.90)];
        spikeThreshold = Math.max(p90 * 2, 10);
    }

    // Step 3: mark spike indices (expand ±1 to catch ramp in/out)
    const spikeSet = new Set();
    for (let i = 0; i < flow.length; i++) {
        if (Math.abs(flow[i]) > spikeThreshold) {
            if (i > 0) spikeSet.add(i - 1);
            spikeSet.add(i);
            if (i < flow.length - 1) spikeSet.add(i + 1);
        }
    }

    // Step 4: build display array — NaN for spikes (Chart.js skips them)
    const flowDisplay = flow.map((v, i) => spikeSet.has(i) ? NaN : v);

    // Step 5: compute clean ceiling from non-spike, non-trivial values
    const cleanAbs = absVals.filter(v => v <= spikeThreshold);
    const p95 = cleanAbs.length > 0 ? cleanAbs[Math.floor(cleanAbs.length * 0.95)] : 5;
    const flowCeiling = Math.max(Math.ceil(p95 * 1.5), 2);

    return { flowDisplay, flowCeiling, spikeSet };
}

// Detect phases from markers + flow data.
// Returns array of { label, startIdx, endIdx, avgFlow, pourAvgFlow, isActive }.
// avgFlow: average over entire phase (for isActive detection).
// pourAvgFlow: average only during active pour (|flow| >= 0.5 g/s), excludes
//   idle/drawdown time within a phase — better comparison against target flow.
function brewComputePhases(series, markers) {
    if (!markers || markers.length === 0) return [];
    const totalSamples = series.weight.length;
    const phases = [];
    const flowThreshold = 0.3; // g/s — below this is considered idle

    // Build segments from markers (marker[i] starts phase, next marker or end closes it)
    const points = markers.map(m => ({ t: m.t, label: m.label }));
    // Add implicit end
    for (let i = 0; i < points.length; i++) {
        const start = points[i].t;
        const end = (i + 1 < points.length) ? points[i + 1].t : totalSamples;
        // Compute avg flow in this segment
        let flowSum = 0, flowCount = 0;
        let pourSum = 0, pourCount = 0;
        for (let j = start; j < end && j < series.flow.length; j++) {
            const af = Math.abs(series.flow[j]);
            if (af < BREW_FLOW_HARD_CEILING) {
                flowSum += af; flowCount++;
                if (af >= BREW_POUR_THRESHOLD) { pourSum += af; pourCount++; }
            }
        }
        const avgFlow = flowCount > 0 ? flowSum / flowCount : 0;
        const pourAvgFlow = pourCount > 0 ? pourSum / pourCount : 0;
        phases.push({
            label: points[i].label,
            startIdx: start,
            endIdx: end,
            avgFlow: avgFlow,
            pourAvgFlow: pourAvgFlow,
            isActive: avgFlow >= flowThreshold
        });
    }
    return phases;
}

// Compute derived metrics from series + fields. Returns array of {label, value}
// only for metrics where source data is available.
function brewComputeDerivedMetrics(series, fields) {
    const metrics = [];
    if (!series || !series.flow || series.flow.length === 0) return metrics;

    const flow = series.flow;
    const intervalSec = (series.interval_ms || 1000) / 1000;
    const { spikeSet } = brewAnalyseFlow(flow);

    // Active pour time: seconds where flow > threshold and not a spike
    let activeSamples = 0;
    const activeFlows = [];
    for (let i = 0; i < flow.length; i++) {
        if (spikeSet.has(i)) continue;
        const af = Math.abs(flow[i]);
        if (af >= BREW_POUR_THRESHOLD) {
            activeSamples++;
            activeFlows.push(af);
        }
    }
    const activePourSec = activeSamples * intervalSec;
    if (activePourSec > 0) {
        const m = Math.floor(activePourSec / 60);
        const s = Math.round(activePourSec % 60);
        metrics.push({ label: 'Active Pour', value: m + ':' + String(s).padStart(2, '0'),
            tooltip: 'Total time where flow rate exceeds 0.5 g/s (excluding swirl spikes).' });
    }

    // Peak and avg flow (spike-filtered, above pour threshold)
    if (activeFlows.length > 0) {
        const peakFlow = Math.max(...activeFlows);
        const avgFlow = activeFlows.reduce((a, b) => a + b, 0) / activeFlows.length;
        metrics.push({ label: 'Peak Flow', value: peakFlow.toFixed(1) + ' g/s',
            tooltip: 'Highest instantaneous flow rate during active pour (spike-filtered).' });
        metrics.push({ label: 'Avg Flow', value: avgFlow.toFixed(1) + ' g/s',
            tooltip: 'Mean flow rate during active pour (spike-filtered).' });
    }

    // Drawdown time: from last active pour sample to end of recording
    let lastActiveIdx = -1;
    for (let i = flow.length - 1; i >= 0; i--) {
        if (spikeSet.has(i)) continue;
        const af = Math.abs(flow[i]);
        if (af >= BREW_POUR_THRESHOLD) { lastActiveIdx = i; break; }
    }
    if (lastActiveIdx >= 0 && lastActiveIdx < flow.length - 2) {
        const drawdownSec = (flow.length - 1 - lastActiveIdx) * intervalSec;
        const m = Math.floor(drawdownSec / 60);
        const s = Math.round(drawdownSec % 60);
        metrics.push({ label: 'Drawdown', value: m + ':' + String(s).padStart(2, '0'),
            tooltip: 'Time from last active pour to end of recording (bed draining).' });
    }

    // Bloom ratio: bloom_water / dose
    const dose = brewFindField(fields, 'dose');
    const bloom = brewFindField(fields, 'bloom_water');
    if (dose && bloom && dose.value > 0) {
        metrics.push({ label: 'Bloom Ratio', value: (bloom.value / dose.value).toFixed(1) + 'x',
            tooltip: 'Bloom water divided by coffee dose. Typical range: 2x–3x.' });
    }

    // Flow consistency: std deviation of active flow rates
    if (activeFlows.length > 2) {
        const mean = activeFlows.reduce((a, b) => a + b, 0) / activeFlows.length;
        const variance = activeFlows.reduce((a, v) => a + (v - mean) * (v - mean), 0) / activeFlows.length;
        const stdev = Math.sqrt(variance);
        metrics.push({ label: 'Flow σ', value: stdev.toFixed(2) + ' g/s',
            tooltip: 'Standard deviation of active flow rates. Lower = more consistent pour.' });
    }

    return metrics;
}

// ============================================================================
// Phase & Chart constants
// ============================================================================

const PHASE_COLORS = [
    '#667eea', '#764ba2', '#43a047', '#FB8C00',
    '#26A69A', '#5C6BC0', '#EC407A', '#8D6E63'
];

// ============================================================================
// Derived Metrics rendering
// ============================================================================

function brewRenderDerivedMetrics(metrics) {
    const el = document.getElementById('brew-derived-metrics');
    if (!metrics || metrics.length === 0) {
        el.style.display = 'none';
        return;
    }
    el.style.display = '';
    el.innerHTML = '';
    for (const m of metrics) {
        const div = document.createElement('div');
        div.className = 'brew-field brew-field-derived';
        if (m.tooltip) div.title = m.tooltip;
        div.innerHTML = '<span class="brew-field-label">' + m.label + '</span>' +
                        '<span class="brew-field-value">' + m.value + '</span>';
        el.appendChild(div);
    }
}

// ============================================================================
// Detail View
// ============================================================================

async function brewShowDetail(id) {
    brewListScroll = window.scrollY;

    try {
        const resp = await fetch(API_BREWS + '?id=' + id);
        if (!resp.ok) throw new Error('Failed to load brew');
        brewDetailData = await resp.json();

        // Populate fields
        const name = brewFindField(brewDetailData.fields, 'template');
        const ts = brewFindField(brewDetailData.fields, 'ts');
        const ti = brewDetailData.template_info;
        const displayName = (ti && ti.display_name) ? ti.display_name : (name ? name.value : 'Brew');
        document.getElementById('brew-detail-name').textContent = '☕ ' + displayName;
        document.getElementById('brew-detail-date').textContent = ts ? brewFormatField(ts) : '';

        // Fields grid (skip template and ts — shown in header)
        const fieldsEl = document.getElementById('brew-detail-fields');
        fieldsEl.innerHTML = '';
        const fieldTooltips = {
            duration: 'Total brew time from first pour to end of recording.',
            water: 'Total water weight at end of brew.',
            dose: 'Dry coffee dose used for the brew.',
            ratio: 'Brew ratio: total water / coffee dose.',
            bloom_water: 'Water added during the bloom phase.'
        };
        const ft = (ti && ti.field_targets) || {};
        for (const field of brewDetailData.fields) {
            if (field.key === 'template' || field.key === 'ts') continue;
            const div = document.createElement('div');
            div.className = 'brew-field';
            if (fieldTooltips[field.key]) div.title = fieldTooltips[field.key];
            let valHtml = brewFormatField(field);
            // Show target annotation when a field_target exists for this key
            if (ft[field.key] != null && typeof field.value === 'number') {
                const target = ft[field.key];
                const delta = field.value - target;
                const sign = delta >= 0 ? '+' : '';
                const cls = Math.abs(delta) <= target * BREW_FIELD_TARGET_TOLERANCE ? 'brew-target-ok' : 'brew-target-miss';
                valHtml += ` <span class="brew-target ${cls}" title="Target: ${target}${field.unit || 'g'}">(${sign}${delta.toFixed(1)})</span>`;
            }
            div.innerHTML = `<span class="brew-field-label">${field.label}</span>
                             <span class="brew-field-value">${valHtml}</span>`;
            fieldsEl.appendChild(div);
        }

        // Derived metrics (computed from series)
        const derivedMetrics = brewComputeDerivedMetrics(brewDetailData.series, brewDetailData.fields);
        brewRenderDerivedMetrics(derivedMetrics);

        // Per-stage flow stats (target vs actual)
        brewRenderPhaseFlowStats(brewDetailData.series, brewDetailData.markers, ti);

        // Charts (phase strip is rendered inside the chart by brewPhaseStripPlugin)
        brewCreateCharts(brewDetailData.series, brewDetailData.markers, ti);

        // Wire collapsible chart toggles
        brewInitToggles();

        // Wire detail actions
        document.getElementById('brew-export-one-btn').onclick = () => brewExportOne(brewDetailData);
        document.getElementById('brew-delete-one-btn').onclick = () => brewDeleteOne(id, true);

        // Switch view
        document.getElementById('brew-list-view').style.display = 'none';
        document.getElementById('brew-detail-view').style.display = 'block';
        window.scrollTo(0, 0);
    } catch (err) {
        console.error('brewShowDetail error:', err);
        if (typeof showMessage === 'function') showMessage('Failed to load brew details', 'error');
    }
}

function brewInitToggles() {
    document.getElementById('brew-weight-toggle').onclick = function () {
        const body = document.getElementById('brew-weight-body');
        const icon = this.querySelector('.brew-toggle-icon');
        const open = body.style.display === 'none';
        body.style.display = open ? '' : 'none';
        icon.textContent = open ? '▾' : '▸';
    };
    document.getElementById('brew-flow-toggle').onclick = function () {
        const body = document.getElementById('brew-flow-body');
        const icon = this.querySelector('.brew-toggle-icon');
        const open = body.style.display === 'none';
        body.style.display = open ? '' : 'none';
        icon.textContent = open ? '▾' : '▸';
    };
}

function brewBackToList() {
    // Destroy charts
    if (weightChart) { weightChart.destroy(); weightChart = null; }
    if (flowChart) { flowChart.destroy(); flowChart = null; }
    if (comboChart) { comboChart.destroy(); comboChart = null; }

    document.getElementById('brew-detail-view').style.display = 'none';
    document.getElementById('brew-list-view').style.display = 'block';
    window.scrollTo(0, brewListScroll);
}

// ============================================================================
// Chart helpers
// ============================================================================

// Phase strip custom Chart.js plugin — draws colored phase bands at the bottom of chart area
const brewPhaseStripPlugin = {
    id: 'brewPhaseStrip',
    afterDraw(chart) {
        const cfg = chart.options.plugins.brewPhaseStrip;
        if (!cfg || !cfg.phases || cfg.phases.length === 0) return;
        const { ctx, chartArea } = chart;
        const xScale = chart.scales.x;
        const maxIdx = chart.data.labels.length - 1;
        const stripH = 18;
        const y = xScale.bottom + 2;
        const palette = cfg.palette || PHASE_COLORS;
        const idleColor = 'rgba(158,158,158,0.25)';
        let colorIdx = 0;
        ctx.save();
        for (const p of cfg.phases) {
            const x1 = xScale.getPixelForValue(Math.max(0, p.startIdx));
            const x2 = p.endIdx > maxIdx ? chartArea.right : xScale.getPixelForValue(p.endIdx);
            const w = x2 - x1;
            if (w < 1) continue;
            const color = p.isActive ? palette[colorIdx++ % palette.length] : null;
            // Fill band with translucent color
            ctx.fillStyle = color ? (color + '30') : idleColor;
            ctx.fillRect(x1, y, w, stripH);
            // Label
            if (w > 20) {
                ctx.fillStyle = color || '#999';
                ctx.font = '600 10px system-ui, sans-serif';
                ctx.textAlign = 'left';
                ctx.textBaseline = 'middle';
                const maxC = Math.floor((w - 6) / 5.8);
                let lbl = p.label;
                if (lbl.length > maxC) lbl = lbl.substring(0, maxC - 1) + '\u2026';
                if (maxC > 0) ctx.fillText(lbl, x1 + 3, y + stripH / 2);
            }
        }
        ctx.restore();
    }
};

function flowColor(val) {
    if (val > 5) return '#EF5350';
    if (val > 3) return '#FFA726';
    if (val >= 1.5) return '#66BB6A';
    return '#9E9E9E';
}

// Build box annotations for swirl/spike regions (shaded vertical bands)
function brewSwirlAnnotations(spikeSet) {
    if (!spikeSet || spikeSet.size === 0) return {};
    // Collapse spikeSet into contiguous runs [start, end]
    const sorted = [...spikeSet].sort((a, b) => a - b);
    const runs = [];
    let rs = sorted[0], re = sorted[0];
    for (let i = 1; i < sorted.length; i++) {
        if (sorted[i] <= re + 1) {
            re = sorted[i];
        } else {
            runs.push([rs, re]);
            rs = sorted[i];
            re = sorted[i];
        }
    }
    runs.push([rs, re]);
    const annotations = {};
    for (let i = 0; i < runs.length; i++) {
        annotations['swirl' + i] = {
            type: 'box',
            xMin: runs[i][0],
            xMax: runs[i][1],
            backgroundColor: 'rgba(255, 152, 0, 0.10)',
            borderColor: 'rgba(255, 152, 0, 0.30)',
            borderWidth: 1,
            label: {
                display: runs[i][1] - runs[i][0] >= 2,
                content: 'swirl',
                position: { x: 'center', y: 'start' },
                yAdjust: -14,
                color: 'rgba(255, 152, 0, 0.75)',
                font: { size: 11, style: 'italic' },
                padding: 2
            }
        };
    }
    return annotations;
}

// Build annotation config for markers (vertical dashed lines)
function brewMarkerAnnotations(markers) {
    if (!markers || markers.length === 0) return {};
    const annotations = {};
    for (let i = 0; i < markers.length; i++) {
        const m = markers[i];
        annotations['marker' + i] = {
            type: 'line',
            xMin: m.t,
            xMax: m.t,
            borderColor: 'rgba(100, 100, 100, 0.5)',
            borderWidth: 1,
            borderDash: [4, 4]
        };
    }
    return annotations;
}

// Build dashed horizontal reference-line annotations for per-phase target flow rates.
// Each active phase whose template target includes a flow_rate gets a line on the y1 axis.
function brewTargetFlowAnnotations(phases, templateInfo) {
    if (!phases || phases.length === 0) return {};
    const targets = (templateInfo && templateInfo.targets) || {};
    if (Object.keys(targets).length === 0) return {};

    const annotations = {};
    let colorIdx = 0;
    for (const phase of phases) {
        if (!phase.isActive) continue;
        const ci = colorIdx++;
        const tgt = targets[phase.label];
        if (!tgt || !tgt.flow_rate) continue;
        const color = PHASE_COLORS[ci % PHASE_COLORS.length];
        annotations['flowTarget' + ci] = {
            type: 'line',
            yMin: tgt.flow_rate,
            yMax: tgt.flow_rate,
            yScaleID: 'y1',
            xMin: phase.startIdx,
            xMax: phase.endIdx - 1,
            borderColor: color + 'AA',
            borderWidth: 1.5,
            borderDash: [6, 4]
        };
    }
    return annotations;
}

// Render per-stage flow stats table: avg flow vs target for each active phase
function brewRenderPhaseFlowStats(series, markers, templateInfo) {
    const el = document.getElementById('brew-phase-flow-stats');
    if (!el) return;
    const targets = (templateInfo && templateInfo.targets) || {};
    if (Object.keys(targets).length === 0 || !series || !series.flow) {
        el.style.display = 'none';
        return;
    }
    const phases = brewComputePhases(series, markers).filter(p => p.isActive);
    // Only show if at least one active phase has a flow_rate target
    const hasAny = phases.some(p => targets[p.label] && targets[p.label].flow_rate > 0);
    if (!hasAny) { el.style.display = 'none'; return; }

    el.style.display = '';
    let html = '<div class="brew-phase-stats-label">Flow by Stage</div><div class="brew-phase-stats-grid">';
    let colorIdx = 0;
    for (const phase of phases) {
        const color = PHASE_COLORS[colorIdx++ % PHASE_COLORS.length];
        const tgt = targets[phase.label];
        const targetFlow = tgt && tgt.flow_rate ? tgt.flow_rate : null;
        const displayFlow = phase.pourAvgFlow;
        let deltaHtml = '';
        if (targetFlow) {
            const delta = displayFlow - targetFlow;
            const sign = delta >= 0 ? '+' : '';
            const cls = Math.abs(delta) <= targetFlow * BREW_FLOW_TARGET_TOLERANCE ? 'brew-target-ok' : 'brew-target-miss';
            deltaHtml = ` <span class="brew-target ${cls}">(${sign}${delta.toFixed(1)})</span>`;
        }
        html += '<div class="brew-phase-stat" style="border-left-color:' + color + '">' +
            '<span class="brew-field-label">' + phase.label + '</span>' +
            '<span class="brew-field-value">' + displayFlow.toFixed(1) + ' g/s' + deltaHtml + '</span>' +
            (targetFlow ? '<span class="brew-phase-stat-target">target ' + targetFlow.toFixed(1) + ' g/s</span>' : '') +
            '</div>';
    }
    html += '</div>';
    el.innerHTML = html;
}

// ============================================================================
// Charts (Chart.js)
// ============================================================================

function brewCreateCharts(series, markers, templateInfo) {
    if (weightChart) { weightChart.destroy(); weightChart = null; }
    if (flowChart) { flowChart.destroy(); flowChart = null; }
    if (comboChart) { comboChart.destroy(); comboChart = null; }

    if (!window.Chart) {
        document.getElementById('brew-combo-fallback').style.display = 'block';
        document.getElementById('brew-weight-fallback').style.display = 'block';
        document.getElementById('brew-flow-fallback').style.display = 'block';
        return;
    }

    // Register phase strip plugin once
    if (!Chart._brewPhaseStripReg) {
        Chart.register(brewPhaseStripPlugin);
        Chart._brewPhaseStripReg = true;
    }

    document.getElementById('brew-combo-fallback').style.display = 'none';
    document.getElementById('brew-weight-fallback').style.display = 'none';
    document.getElementById('brew-flow-fallback').style.display = 'none';

    if (!series || !series.weight || series.weight.length === 0) return;

    const intervalSec = (series.interval_ms || 1000) / 1000;
    const labels = series.weight.map((_, i) => (i * intervalSec).toFixed(0));
    const flowRaw = series.flow || [];
    const { flowDisplay, flowCeiling, spikeSet } = brewAnalyseFlow(flowRaw);
    const markerAnns = brewMarkerAnnotations(markers);
    const swirlAnns = brewSwirlAnnotations(spikeSet);

    // Compute phases for in-chart strip
    const phases = brewComputePhases(series, markers)
        .filter(p => p.endIdx > p.startIdx);

    // Build target flow rate reference lines per phase
    const targetFlowAnns = brewTargetFlowAnnotations(phases, templateInfo);
    const comboAnnotations = Object.assign({}, markerAnns, swirlAnns, targetFlowAnns);
    const basicAnnotations = Object.assign({}, markerAnns, swirlAnns);

    // Compute nice tick step in seconds
    const totalDurSec = parseFloat(labels[labels.length - 1]) || 1;
    const niceSteps = [5, 10, 15, 20, 30, 60, 120];
    let tickStepSec = 120;
    for (const s of niceSteps) { if (totalDurSec / s <= 14) { tickStepSec = s; break; } }

    // Align weight & flow Y-axis grid lines.
    // Pick a nice round step for each axis, then equalise interval count
    // so both axes share the same grid rows.
    function niceStep(maxVal, targetIntervals) {
        const rough = maxVal / targetIntervals;
        const mag = Math.pow(10, Math.floor(Math.log10(rough)));
        const norm = rough / mag;
        if (norm <= 1) return 1 * mag;
        if (norm <= 2) return 2 * mag;
        if (norm <= 5) return 5 * mag;
        return 10 * mag;
    }
    const weightMax = Math.max(...series.weight) || 1;
    const wStep = niceStep(weightMax, 6);
    const fStep = niceStep(flowCeiling, 6);
    const intervals = Math.max(Math.ceil(weightMax / wStep), Math.ceil(flowCeiling / fStep));
    const alignedWeightMax = intervals * wStep;
    const alignedFlowMax = intervals * fStep;

    // ---- Combined Chart (hero) ----
    const comboCtx = document.getElementById('brew-combo-chart').getContext('2d');
    comboChart = new Chart(comboCtx, {
        type: 'line',
        data: {
            labels: labels,
            datasets: [
                {
                    label: 'Weight (g)',
                    data: series.weight,
                    borderColor: '#667eea',
                    backgroundColor: 'rgba(102, 126, 234, 0.08)',
                    fill: true,
                    tension: 0.3,
                    pointRadius: 0,
                    pointHitRadius: 10,
                    borderWidth: 2,
                    yAxisID: 'y'
                },
                {
                    label: 'Flow (g/s)',
                    data: flowDisplay,
                    segment: {
                        borderColor: (ctx) => flowColor(ctx.p1.parsed.y)
                    },
                    borderColor: '#9E9E9E',
                    fill: false,
                    tension: 0.3,
                    pointRadius: 0,
                    pointHitRadius: 10,
                    borderWidth: 1.5,
                    spanGaps: false,
                    yAxisID: 'y1'
                }
            ]
        },
        options: {
            responsive: true,
            maintainAspectRatio: true,
            aspectRatio: 2.2,
            layout: { padding: { top: 18, bottom: 40 } },
            interaction: { mode: 'index', intersect: false },
            plugins: {
                legend: { display: false },
                tooltip: {
                    callbacks: {
                        title: (items) => items[0].label + 's',
                        label: (item) => {
                            if (item.datasetIndex === 0)
                                return 'Weight: ' + item.parsed.y.toFixed(1) + ' g';
                            const realVal = flowRaw[item.dataIndex];
                            if (realVal == null || isNaN(flowDisplay[item.dataIndex])) return null;
                            return 'Flow: ' + realVal.toFixed(2) + ' g/s';
                        }
                    }
                },
                annotation: { annotations: comboAnnotations, clip: false },
                brewPhaseStrip: { phases: phases, palette: PHASE_COLORS }
            },
            scales: {
                x: {
                    title: { display: true, text: 'Time (s)' },
                    ticks: {
                        autoSkip: true,
                        maxRotation: 0,
                        callback: function(val, idx) {
                            const sec = idx * intervalSec;
                            if (Math.abs(sec - Math.round(sec / tickStepSec) * tickStepSec) < intervalSec * 0.6) {
                                return Math.round(sec);
                            }
                            return null;
                        },
                        color: function(ctx) {
                            if (!phases || phases.length === 0) return '#666';
                            const idx = ctx.tick.value;
                            let ci = 0;
                            for (const p of phases) {
                                if (p.endIdx <= p.startIdx) continue;
                                if (idx >= p.startIdx && idx < p.endIdx) {
                                    return p.isActive ? PHASE_COLORS[ci % PHASE_COLORS.length] : '#999';
                                }
                                if (p.isActive) ci++;
                            }
                            return '#666';
                        }
                    }
                },
                y: {
                    type: 'linear',
                    position: 'left',
                    title: { display: true, text: 'Weight (g)' },
                    min: 0,
                    max: alignedWeightMax,
                    ticks: { stepSize: wStep }
                },
                y1: {
                    type: 'linear',
                    position: 'right',
                    title: { display: true, text: 'Flow (g/s)' },
                    min: 0,
                    max: alignedFlowMax,
                    ticks: { stepSize: fStep },
                    grid: { drawOnChartArea: false }
                }
            }
        }
    });

    // ---- Individual Weight Chart ----
    const weightCtx = document.getElementById('brew-weight-chart').getContext('2d');
    weightChart = new Chart(weightCtx, {
        type: 'line',
        data: {
            labels: labels,
            datasets: [{
                label: 'Weight (g)',
                data: series.weight,
                borderColor: '#667eea',
                backgroundColor: 'rgba(102, 126, 234, 0.1)',
                fill: true,
                tension: 0.3,
                pointRadius: 0,
                pointHitRadius: 10,
                borderWidth: 2
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: true,
            aspectRatio: 2.5,
            interaction: { mode: 'index', intersect: false },
            plugins: {
                legend: { display: false },
                tooltip: {
                    callbacks: {
                        title: (items) => items[0].label + 's',
                        label: (item) => item.parsed.y.toFixed(1) + ' g'
                    }
                },
                annotation: { annotations: basicAnnotations }
            },
            scales: {
                x: {
                    title: { display: true, text: 'Time (s)' },
                    ticks: { maxTicksLimit: 10 }
                },
                y: {
                    title: { display: true, text: 'Weight (g)' },
                    beginAtZero: true
                }
            }
        }
    });

    // ---- Individual Flow Rate Chart ----
    const flowCtx = document.getElementById('brew-flow-chart').getContext('2d');
    flowChart = new Chart(flowCtx, {
        type: 'line',
        data: {
            labels: labels,
            datasets: [{
                label: 'Flow (g/s)',
                data: flowDisplay,
                segment: {
                    borderColor: (ctx) => flowColor(ctx.p1.parsed.y)
                },
                borderColor: '#9E9E9E',
                fill: false,
                tension: 0.3,
                pointRadius: 0,
                pointHitRadius: 10,
                borderWidth: 2,
                spanGaps: false
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: true,
            aspectRatio: 2.5,
            interaction: { mode: 'index', intersect: false },
            plugins: {
                legend: { display: false },
                tooltip: {
                    callbacks: {
                        title: (items) => items[0].label + 's',
                        label: (item) => {
                            const realVal = flowRaw[item.dataIndex];
                            if (realVal == null || isNaN(flowDisplay[item.dataIndex])) return null;
                            return realVal.toFixed(2) + ' g/s';
                        }
                    }
                },
                annotation: { annotations: basicAnnotations }
            },
            scales: {
                x: {
                    title: { display: true, text: 'Time (s)' },
                    ticks: { maxTicksLimit: 10 }
                },
                y: {
                    title: { display: true, text: 'Flow (g/s)' },
                    min: 0,
                    max: flowCeiling
                }
            }
        }
    });
}

// ============================================================================
// Delete
// ============================================================================

async function brewDeleteOne(id, fromDetail) {
    if (!confirm('Delete this brew?')) return;

    try {
        const resp = await fetch(API_BREWS + '?id=' + id, { method: 'DELETE' });
        if (!resp.ok) throw new Error('Delete failed');

        if (fromDetail) {
            brewBackToList();
        }
        brewLoadList();
    } catch (err) {
        console.error('brewDeleteOne error:', err);
        if (typeof showMessage === 'function') showMessage('Failed to delete brew', 'error');
    }
}

async function brewClearAll() {
    if (!confirm('Delete ALL brews? This cannot be undone.')) return;

    try {
        const resp = await fetch(API_BREWS, { method: 'DELETE' });
        if (!resp.ok) throw new Error('Clear failed');
        brewLoadList();
    } catch (err) {
        console.error('brewClearAll error:', err);
        if (typeof showMessage === 'function') showMessage('Failed to clear brews', 'error');
    }
}

// ============================================================================
// Export
// ============================================================================

function brewExportOne(brew) {
    // Strip the id (not stored in file)
    const exportData = { v: brew.v, fields: brew.fields };
    if (brew.markers && brew.markers.length > 0) exportData.markers = brew.markers;
    if (brew.template_info) exportData.template_info = brew.template_info;
    exportData.series = brew.series;
    const blob = new Blob([JSON.stringify(exportData, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 'brew_' + String(brew.id).padStart(4, '0') + '.json';
    a.click();
    URL.revokeObjectURL(url);
}

async function brewExportAll() {
    if (!brewListData || !brewListData.brews || brewListData.brews.length === 0) {
        if (typeof showMessage === 'function') showMessage('No brews to export', 'info');
        return;
    }

    const btn = document.getElementById('brew-export-all-btn');
    btn.disabled = true;
    btn.textContent = 'Exporting...';

    try {
        const allBrews = [];
        for (const brew of brewListData.brews) {
            const resp = await fetch(API_BREWS + '?id=' + brew.id);
            if (resp.ok) {
                const data = await resp.json();
                const item = { v: data.v, fields: data.fields };
                if (data.markers && data.markers.length > 0) item.markers = data.markers;
                if (data.template_info) item.template_info = data.template_info;
                item.series = data.series;
                allBrews.push(item);
            }
        }

        const blob = new Blob([JSON.stringify(allBrews, null, 2)], { type: 'application/json' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = 'brews_export.json';
        a.click();
        URL.revokeObjectURL(url);
    } catch (err) {
        console.error('brewExportAll error:', err);
        if (typeof showMessage === 'function') showMessage('Export failed', 'error');
    } finally {
        btn.disabled = false;
        btn.textContent = 'Export All';
    }
}

// ============================================================================
// Import
// ============================================================================

async function brewImport(file) {
    const text = await file.text();

    // Client-side validation
    try {
        const data = JSON.parse(text);
        const items = Array.isArray(data) ? data : [data];
        for (const item of items) {
            if (!item.v || !item.fields || !item.series) {
                if (typeof showMessage === 'function') showMessage('Invalid brew format: missing v, fields, or series', 'error');
                return;
            }
        }
    } catch (e) {
        if (typeof showMessage === 'function') showMessage('Invalid JSON file', 'error');
        return;
    }

    try {
        const resp = await fetch(API_BREWS + '/import', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: text
        });
        if (!resp.ok) throw new Error('Import failed');
        const result = await resp.json();
        if (typeof showMessage === 'function') showMessage('Imported ' + result.imported + ' brew(s)', 'success');
        brewLoadList();
    } catch (err) {
        console.error('brewImport error:', err);
        if (typeof showMessage === 'function') showMessage('Import failed', 'error');
    }
}

// ============================================================================
// Init
// ============================================================================

document.addEventListener('DOMContentLoaded', () => {
    if (typeof initNavigation === 'function') initNavigation();
    if (typeof loadMode === 'function') loadMode();
    if (typeof loadDeviceInfo === 'function') loadDeviceInfo();
    if (typeof loadVersion === 'function') loadVersion();
    if (typeof initHealthWidget === 'function') initHealthWidget();

    // Only run on brews page
    if (currentPage !== 'brews') return;

    // Hide loading overlay (brews page doesn't use config form)
    const overlay = document.getElementById('form-loading-overlay');
    if (overlay) overlay.style.display = 'none';

    // Wire action bar buttons
    document.getElementById('brew-export-all-btn').addEventListener('click', brewExportAll);
    document.getElementById('brew-clear-all-btn').addEventListener('click', brewClearAll);
    document.getElementById('brew-back-btn').addEventListener('click', brewBackToList);

    // Import button triggers hidden file input
    document.getElementById('brew-import-btn').addEventListener('click', () => {
        document.getElementById('brew-import-file').click();
    });

    // Import file picker
    document.getElementById('brew-import-file').addEventListener('change', (e) => {
        const file = e.target.files[0];
        if (file) {
            brewImport(file);
            e.target.value = '';  // reset for re-import
        }
    });

    // Load brew list
    brewLoadList();
});
