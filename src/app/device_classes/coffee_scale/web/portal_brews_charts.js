// portal_brews_charts.js - Series analysis, chart rendering, and derived metrics
// Extracted from portal_brews.js for maintainability.

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

    // Step 3: mark spike indices (expand ±2 to catch ramp-in and settle tail)
    const spikeSet = new Set();
    for (let i = 0; i < flow.length; i++) {
        if (Math.abs(flow[i]) > spikeThreshold) {
            for (let d = -2; d <= 2; d++) {
                const j = i + d;
                if (j >= 0 && j < flow.length) spikeSet.add(j);
            }
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

// Compute ideal weight curve from template targets + markers.
// Walks stages in marker order, builds 1-sample-per-second weight ramp/plateau.
// Returns number[] aligned with series length, or null if insufficient data.
function brewComputeIdealCurve(series, markers, templateInfo) {
    if (!series || !series.weight || series.weight.length === 0) return null;
    const targets = (templateInfo && templateInfo.targets) || {};
    if (!markers || markers.length === 0 || Object.keys(targets).length === 0) return null;

    const totalSamples = series.weight.length;
    const curve = new Array(totalSamples);
    let t = 0;          // current sample index
    let w = 0;          // current ideal weight

    for (let i = 0; i < markers.length; i++) {
        const label = markers[i].label;
        const tgt = targets[label];
        if (!tgt || tgt.weight == null) { continue; }

        const targetWeight = tgt.weight;
        const flowRate = tgt.flow_rate || 0;
        const timeS = tgt.time_s || 0;

        if (flowRate > 0) {
            // Ramp: pour at flow_rate until target weight
            const pourDuration = Math.ceil((targetWeight - w) / flowRate);
            const pourEnd = Math.min(t + pourDuration, totalSamples);
            const startW = w;
            for (; t < pourEnd; t++) {
                w = startW + (t - (pourEnd - pourDuration)) * flowRate;
                curve[t] = Math.min(w, targetWeight);
            }
            w = targetWeight;
        }

        if (timeS > 0) {
            // Plateau: hold at current weight until time_s elapsed from marker
            const markerT = markers[i].t || 0;
            const plateauEnd = Math.min(markerT + timeS, totalSamples);
            for (; t < plateauEnd; t++) {
                curve[t] = w;
            }
        }
    }

    // Pad remaining samples at final weight (drawdown / hold)
    for (; t < totalSamples; t++) {
        curve[t] = w;
    }

    // Only return if we managed to compute something meaningful
    return w > 0 ? curve : null;
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

    // Compute ideal weight curve from template targets
    const idealCurve = brewComputeIdealCurve(series, markers, templateInfo);

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
                ...(idealCurve ? [{
                    label: 'Target (g)',
                    data: idealCurve,
                    borderColor: 'rgba(102, 126, 234, 0.35)',
                    borderDash: [2, 3],
                    fill: false,
                    tension: 0,
                    pointRadius: 0,
                    pointHitRadius: 0,
                    borderWidth: 1.5,
                    yAxisID: 'y'
                }] : []),
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
                            const ds = item.dataset;
                            if (ds.label === 'Weight (g)')
                                return 'Weight: ' + item.parsed.y.toFixed(1) + ' g';
                            if (ds.label === 'Target (g)')
                                return 'Target: ' + item.parsed.y.toFixed(1) + ' g';
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
            datasets: [
                {
                    label: 'Weight (g)',
                    data: series.weight,
                    borderColor: '#667eea',
                    backgroundColor: 'rgba(102, 126, 234, 0.1)',
                    fill: true,
                    tension: 0.3,
                    pointRadius: 0,
                    pointHitRadius: 10,
                    borderWidth: 2
                },
                ...(idealCurve ? [{
                    label: 'Target (g)',
                    data: idealCurve,
                    borderColor: 'rgba(102, 126, 234, 0.35)',
                    borderDash: [2, 3],
                    fill: false,
                    tension: 0,
                    pointRadius: 0,
                    pointHitRadius: 0,
                    borderWidth: 1.5
                }] : [])
            ]
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

    // Show/hide ideal curve legend entry
    const idealLegendEl = document.getElementById('brew-legend-ideal');
    if (idealLegendEl) idealLegendEl.style.display = idealCurve ? '' : 'none';
}

