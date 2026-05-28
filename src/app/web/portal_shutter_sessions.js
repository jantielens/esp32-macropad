// portal_shutter_sessions.js — Sessions view: list, detail, waveform charts.
// Integrated via portal_fragment_init.js init_shutter_sessions_fragment().

// ============================================================================
// State
// ============================================================================

var _sessionsCurrentId   = null;
var _sessionsCurrentData = null;
var _sessionCharts       = {};  // Chart.js instances keyed by chart ID
var _sessionHoverGuideRegistered = false;
var _verdictThresholds   = null; // verdict_thresholds from GET /api/config

// Exposure-sim sweep orientation (session-scoped, not persisted).
// Values: 'horizontal' | 'vertical'. Default: horizontal focal-plane.
var _sweepOrientation = 'horizontal';

// Per-card exposure-sim animation state, keyed by measurement idx.
// { rafId, startTs, elapsedBeforePause, state, holdTimerId, model, exposureImage }
var _exposureAnimations = {};

// Per-measurement exposure image cache, keyed by idx + sweep orientation.
// Cleared on orientation toggle and session navigation.
var _exposureImageCache = {};

// Sensor palette: fill (combined chart background), faint (line color).
// Defined here so all chart creators can reach it without forward refs.
var _WF_SENSOR_PALETTE = [
    { fill: 'rgba(68,170,255,0.30)',  faint: 'rgba(68,170,255,0.65)'  },
    { fill: 'rgba(68,204,68,0.30)',   faint: 'rgba(68,204,68,0.65)'   },
    { fill: 'rgba(160,100,240,0.30)', faint: 'rgba(160,100,240,0.65)' },
    { fill: 'rgba(255,160,64,0.30)',  faint: 'rgba(255,160,64,0.65)'  }
];

// ============================================================================
// Common helpers
// ============================================================================

// Signed numeric prefix: '+' for non-negative, '' for negative.
// Pass only known-numeric values (null returns '+', undefined returns '').
function _signPrefix(v) { return v >= 0 ? '+' : ''; }

// Film diagonal constant for frame capping derivation (matches firmware SHUTTER_FILM_DIAGONAL_MM).
var _FILM_DIAGONAL_MM = 43.27;

// 35mm gate dimensions for 2D capping extrapolation (used by exposure sim too).
var _GATE_W = 36;
var _GATE_H = 24;

// Cell-coloring thresholds for secondary metrics (display-only — they do not
// affect the verdict). Values mirror the previously-shipped firmware defaults
// so existing users see consistent coloring after the verdict simplification.
var _CELL_CAPPING_WARN_STOPS = 0.333;
var _CELL_CAPPING_FAIL_STOPS = 0.667;
var _CELL_SPREAD_WARN_PCT    = 3.0;
var _CELL_SPREAD_FAIL_PCT    = 5.0;
var _CELL_REPEAT_WARN_PCT    = 5.0;
var _CELL_REPEAT_FAIL_PCT    = 10.0;

// Compute full-frame capping estimate (stops) from a measurement array.
// Returns null when no valid gradient data is present.
function _frameCappingFromMeasurements(measurements) {
    var cappingSum = 0, cappingN = 0;
    var cappingXSum = 0, cappingYSum = 0, capping2dN = 0;
    for (var i = 0; i < measurements.length; i++) {
        var m = measurements[i];
        var cg = m.capping_gradient_stops_per_mm;
        if (typeof cg === 'number' && cg >= 0) { cappingSum += cg; cappingN++; }
        var cgx = m.capping_gradient_x, cgy = m.capping_gradient_y;
        if (typeof cgx === 'number' && cgx >= 0 && typeof cgy === 'number' && cgy >= 0) {
            cappingXSum += cgx; cappingYSum += cgy; capping2dN++;
        }
    }
    if (capping2dN > 0) {
        var avgCapX = cappingXSum / capping2dN;
        var avgCapY = cappingYSum / capping2dN;
        var capXS = avgCapX * _GATE_W, capYS = avgCapY * _GATE_H;
        return Math.sqrt(capXS * capXS + capYS * capYS);
    }
    if (cappingN > 0) {
        var avgCap = cappingSum / cappingN;
        return avgCap >= 0.001 ? avgCap * _FILM_DIAGONAL_MM : 0;
    }
    return null;
}

// Return CSS class for a metric cell based on display-only thresholds.
// metricKey: 'deviation'|'frame_capping'|'spread'|'repeatability'.
// Coloring is informational — only 'deviation' drives the verdict (see _rowVerdict).
// NOTE: deviation uses device-configured thresholds from _verdictThresholds (runtime
// API response); secondary metrics use hardcoded portal constants (_CELL_*) since
// they are no longer part of the configurable verdict system.
function _metricColor(value, metricKey) {
    if (value === null || value === undefined) return '';
    var warn, fail, v;
    if (metricKey === 'deviation') {
        if (!_verdictThresholds || !_verdictThresholds.deviation) return '';
        warn = _verdictThresholds.deviation.warning;
        fail = _verdictThresholds.deviation.fail;
        v = Math.abs(value);
    } else if (metricKey === 'frame_capping') {
        warn = _CELL_CAPPING_WARN_STOPS; fail = _CELL_CAPPING_FAIL_STOPS; v = value;
    } else if (metricKey === 'spread') {
        warn = _CELL_SPREAD_WARN_PCT;    fail = _CELL_SPREAD_FAIL_PCT;    v = value;
    } else if (metricKey === 'repeatability') {
        warn = _CELL_REPEAT_WARN_PCT;    fail = _CELL_REPEAT_FAIL_PCT;    v = value;
    } else {
        return '';
    }
    if (v > fail) return 'sst-cell-red';
    if (v > warn) return 'sst-cell-orange';
    return 'sst-cell-green';
}

// Compute the deviation-only verdict for a row/group.
// SYNC: keep in sync with evaluate_verdict() in shutter_measure.cpp.
function _rowVerdict(avgDevStops) {
    var warn = 0.333, fail = 0.500;
    if (_verdictThresholds && _verdictThresholds.deviation) {
        warn = _verdictThresholds.deviation.warning;
        fail = _verdictThresholds.deviation.fail;
    }
    var ad = Math.abs(avgDevStops);
    if (ad > fail) return 'fail';
    if (ad > warn) return 'warning';
    return 'pass';
}

// Compute the verdict for a group of measurements at a given nominal speed.
// Derives mean signed deviation and feeds it to _rowVerdict.
function _computeGroupVerdict(measurements) {
    var n = measurements.length;
    if (n === 0) return 'pass';
    var sumDev = 0, count = 0;
    for (var i = 0; i < n; i++) {
        var d = measurements[i].deviation_stops;
        if (typeof d === 'number') { sumDev += d; count++; }
    }
    var avgDev = count > 0 ? sumDev / count : 0;
    return _rowVerdict(avgDev);
}

// Value of a waveform sample at index i (flat array of averaged values).
function _wfMidpoint(wf, i) {
    return wf[i];
}

function _sessionEnsureHoverGuidePlugin() {
    if (_sessionHoverGuideRegistered || !window.Chart) return;

    var hoverGuidePlugin = {
        id: 'sessionHoverGuide',
        afterEvent: function(chart, args, pluginOptions) {
            if (!pluginOptions || pluginOptions.enabled === false) return;
            var event = args.event;
            var area = chart.chartArea;
            if (!event || !area) return;

            var nextX = null;
            var nextValue = null;
            if (event.type !== 'mouseout' && event.type !== 'mouseleave' &&
                    typeof event.x === 'number' && typeof event.y === 'number' &&
                    event.x >= area.left && event.x <= area.right &&
                    event.y >= area.top && event.y <= area.bottom) {
                nextX = event.x;
                var xScale = chart.scales && chart.scales.x;
                if (xScale && typeof xScale.getValueForPixel === 'function') {
                    nextValue = xScale.getValueForPixel(event.x);
                }
            }

            var prevX = chart.$hoverGuideX;
            var prevValue = chart.$hoverGuideValue;
            if (prevX !== nextX || prevValue !== nextValue) {
                chart.$hoverGuideX = nextX;
                chart.$hoverGuideValue = nextValue;
                args.changed = true;
                // Drive exposure-sim scrubbing for the owning measurement card.
                if (typeof pluginOptions.measurementIdx === 'number'
                        && typeof window._sessionOnWaveformHover === 'function') {
                    window._sessionOnWaveformHover(pluginOptions.measurementIdx, nextValue);
                }
            }
        },
        afterDraw: function(chart, args, pluginOptions) {
            if (!pluginOptions || pluginOptions.enabled === false) return;
            var area = chart.chartArea;
            var x = chart.$hoverGuideX;
            var value = chart.$hoverGuideValue;
            if (!area || typeof x !== 'number') return;

            var ctx = chart.ctx;
            var lineColor = pluginOptions.lineColor || 'rgba(255,255,255,0.45)';
            var labelBg = pluginOptions.labelBackgroundColor || 'rgba(0,0,0,0.75)';
            var labelColor = pluginOptions.labelColor || '#fff';
            var unit = pluginOptions.unit || 'ms';
            var decimals = typeof pluginOptions.decimals === 'number' ? pluginOptions.decimals : 2;

            var labelText = '';
            if (typeof pluginOptions.formatter === 'function') {
                labelText = pluginOptions.formatter(value, unit, decimals);
            } else if (typeof value === 'number') {
                if (unit === 'ms') {
                    labelText = value.toFixed(decimals) + ' ms';
                } else {
                    labelText = value.toFixed(decimals);
                }
            }
            if (!labelText) return;

            ctx.save();

            ctx.strokeStyle = lineColor;
            ctx.lineWidth = 1;
            ctx.beginPath();
            ctx.moveTo(x, area.top);
            ctx.lineTo(x, area.bottom);
            ctx.stroke();

            ctx.font = '11px sans-serif';
            ctx.textBaseline = 'middle';
            var padX = 6;
            var padY = 4;
            var textWidth = ctx.measureText(labelText).width;
            var boxW = textWidth + padX * 2;
            var boxH = 18;
            var bx = x + 8;
            if (bx + boxW > area.right) {
                bx = x - boxW - 8;
            }
            if (bx < area.left) {
                bx = area.left;
            }
            var by = area.top + 4;

            ctx.fillStyle = labelBg;
            ctx.fillRect(bx, by, boxW, boxH);

            ctx.fillStyle = labelColor;
            ctx.fillText(labelText, bx + padX, by + boxH / 2);
            ctx.restore();
        }
    };

    Chart.register(hoverGuidePlugin);
    _sessionHoverGuideRegistered = true;
}

// Group measurements by nearest_speed, preserving first-seen order.
// Returns { order: [speed,...], groups: { speed: { label, nominal_ms, indices, measurements } } }.
function _groupBySpeed(ms) {
    var groups = {}, order = [];
    for (var i = 0; i < ms.length; i++) {
        var m = ms[i];
        var spd = m.nearest_speed || '\u2014';
        if (!groups[spd]) {
            groups[spd] = { label: spd, nominal_ms: m.nearest_duration_ms || 0, indices: [], measurements: [] };
            order.push(spd);
        }
        groups[spd].indices.push(i);
        groups[spd].measurements.push(m);
    }
    return { order: order, groups: groups };
}

// Linear regression slope. Returns null if fewer than 2 points or zero variance.
function _linearRegressionSlope(points) {
    var n = points.length;
    if (n < 2) return null;
    var sumX = 0, sumY = 0, sumXY = 0, sumXX = 0;
    for (var i = 0; i < n; i++) {
        sumX  += points[i].x;
        sumY  += points[i].y;
        sumXY += points[i].x * points[i].y;
        sumXX += points[i].x * points[i].x;
    }
    var denom = n * sumXX - sumX * sumX;
    return Math.abs(denom) > 1e-10 ? (n * sumXY - sumX * sumY) / denom : null;
}

// Populate curtain-edge metrics for every valid sensor in `ms` from
// pre-computed curtain_stats.  Sets sensors[si]._edges or null.
// Call once per detail-view render so downstream code reads s._edges directly.
// Only uses device-side pre-computed data; old sessions without curtain_stats
// will have _edges = null (no JS-side fallback computation).
function _annotateMeasurementEdges(ms) {
    for (var i = 0; i < ms.length; i++) {
        var m = ms[i];
        var sensors = m.sensors || [];
        for (var si = 0; si < sensors.length; si++) {
            var s = sensors[si];
            if (!s.valid) { s._edges = null; continue; }

            var cs = s.curtain_stats;
            if (!cs || typeof cs.curtain1_ms !== 'number') {
                s._edges = null;
                continue;
            }

            s._edges = {
                curtain1Ms:    cs.curtain1_ms,
                dwellMs:       cs.dwell_ms,
                curtain2Ms:    cs.curtain2_ms,
                // curtainRatio is suppressed when device flagged the stats as
                // physically meaningless (full-open mode or sensor recovery
                // tail). Raw c1/dwell/c2 stay available for diagnostics.
                curtainRatio:  (cs.valid !== false && typeof cs.curtain_ratio === 'number')
                                   ? cs.curtain_ratio : null,
                curtain1Frac:    cs.curtain1_start_frac,
                curtain1EndFrac: cs.curtain1_end_frac,
                dwellFrac:       cs.curtain1_end_frac,
                dwellEndFrac:    cs.curtain2_start_frac,
                curtain2Frac:    cs.curtain2_start_frac,
                curtain2EndFrac: cs.curtain2_end_frac
            };
        }
    }
}

// ============================================================================
// CDN loader (shared by fragment init and chart creators)
// ============================================================================

function _sessionLoadScript(src) {
    return new Promise(function(resolve, reject) {
        if (document.querySelector('script[src="' + src + '"]')) { resolve(); return; }
        var s = document.createElement('script');
        s.src = src; s.onload = resolve; s.onerror = reject;
        document.head.appendChild(s);
    });
}

// ============================================================================
// Chart lifecycle
// ============================================================================

function sessionDestroyAllCharts() {
    var keys = Object.keys(_sessionCharts);
    for (var k = 0; k < keys.length; k++) {
        try { _sessionCharts[keys[k]].destroy(); } catch (e) {}
    }
    _sessionCharts = {};
    // Cancel any in-flight exposure-sim animations so their rAF/setTimeout
    // callbacks do not run against the next session's DOM.
    var animKeys = Object.keys(_exposureAnimations);
    for (var ak = 0; ak < animKeys.length; ak++) {
        _cancelExposureAnimation(animKeys[ak]);
    }
}

// Force every Chart.js instance to resize against its current container.
// Used by the beforeprint/afterprint handlers so charts redraw at the print
// page width instead of being stretched/clipped from their on-screen bitmap.
// Exposed on window so portal_fragment_init.js can invoke it.
function sessionResizeAllCharts() {
    var keys = Object.keys(_sessionCharts);
    for (var k = 0; k < keys.length; k++) {
        try { _sessionCharts[keys[k]].resize(); } catch (e) {}
    }
}

// Recreate all charts in the current detail view (e.g. after a theme change).
function sessionRecreateAllCharts() {
    var data = _sessionsCurrentData;
    if (!data) return;  // list view or no data loaded — nothing to do
    var ms = data.measurements || [];
    sessionDestroyAllCharts();
    for (var i = 0; i < ms.length; i++) {
        sessionCreateWaveformCharts(ms[i], i);
        renderExposureSim(data, ms[i], i);
    }
    sessionCreateDeviationChart('session-deviation-chart', ms);
}

// Update sweep orientation and re-render exposure sims only.
// No persistence, no chart recreation needed (canvas-only redraw).
function sessionSetSweepOrientation(value) {
    _sweepOrientation = (value === 'vertical') ? 'vertical' : 'horizontal';
    _exposureImageCache = {};
    var data = _sessionsCurrentData;
    if (!data) return;
    var ms = data.measurements || [];
    // Cancel any in-flight animations — they were built against the previous axis.
    for (var i = 0; i < ms.length; i++) {
        _cancelExposureAnimation(i);
        renderExposureSim(data, ms[i], i);
    }
}

// Toggle sweep orientation from the in-canvas flip icon. Session-scoped:
// flipping any card flips them all. Cancels active animations as they were
// built against the previous travel axis.
function sessionToggleSweepOrientation() {
    sessionSetSweepOrientation(_sweepOrientation === 'vertical' ? 'horizontal' : 'vertical');
}

// ============================================================================
// List view
// ============================================================================

function sessionsShowList() {
    sessionDestroyAllCharts();
    _sessionsCurrentId = null;
    _sessionsCurrentData = null;
    _exposureImageCache = {};
    _sessionsBackToTopTeardown();
    var listView = document.getElementById('sessions-list-view');
    var detailView = document.getElementById('sessions-detail-view');
    if (listView) listView.style.display = '';
    if (detailView) detailView.style.display = 'none';
    sessionsLoad();
}

function sessionsShowDetail(id) {
    _sessionsCurrentId = id;
    var listView = document.getElementById('sessions-list-view');
    var detailView = document.getElementById('sessions-detail-view');
    if (listView) listView.style.display = 'none';
    if (detailView) detailView.style.display = '';
    _sessionsBackToTopSetup();
    sessionsLoadDetail(id);
}

function _sessionsBackToTopSetup() {
    _sessionsBackToTopTeardown();
    var btn = document.createElement('button');
    btn.id = 'sessions-back-to-top';
    btn.title = 'Back to top';
    btn.textContent = '\u2191';
    document.body.appendChild(btn);
    var pane = document.getElementById('content-pane');
    function onScroll() {
        btn.classList.toggle('visible', pane ? pane.scrollTop > 200 : false);
    }
    btn.addEventListener('click', function() {
        if (pane) pane.scrollTo({ top: 0, behavior: 'smooth' });
    });
    if (pane) pane.addEventListener('scroll', onScroll);
    window._sessionsBackToTopCleanup = function() {
        if (pane) pane.removeEventListener('scroll', onScroll);
        btn.remove();
    };
}

function _sessionsBackToTopTeardown() {
    if (window._sessionsBackToTopCleanup) {
        window._sessionsBackToTopCleanup();
        window._sessionsBackToTopCleanup = null;
    }
    var old = document.getElementById('sessions-back-to-top');
    if (old) old.remove();
}

function sessionsLoad() {
    var loadingEl = document.getElementById('sessions-loading');
    var emptyEl   = document.getElementById('sessions-empty');
    var listEl    = document.getElementById('sessions-list');
    if (!listEl) return;

    if (loadingEl) loadingEl.style.display = '';
    if (emptyEl)   emptyEl.style.display = 'none';
    listEl.innerHTML = '';

    fetch('/api/sessions')
        .then(function(r) { return r.ok ? r.json() : null; })
        .then(function(data) {
            if (loadingEl) loadingEl.style.display = 'none';
            if (!data || !Array.isArray(data.entries) || data.entries.length === 0) {
                if (emptyEl) emptyEl.style.display = '';
                return;
            }
            var entries = data.entries; // already sorted newest-first by FsIndexedStore
            var html = '';
            for (var i = 0; i < entries.length; i++) {
                var e = entries[i];
                var date = e.started_at ? new Date(e.started_at * 1000).toLocaleString() : '—';
                var camera = e.camera ? escapeHtml(e.camera) : '<em class="text-muted">Unnamed camera</em>';
                var idNum = (e.id || '').replace(/^sess_/, '');
                html += '<div class="border rounded p-2 mb-2 d-flex align-items-center justify-content-between" style="cursor:pointer" onclick="sessionsShowDetail(\'' + escapeHtml(e.id) + '\')">';
                html += '<div>';
                html += '<div class="fw-semibold">ID: ' + escapeHtml(idNum) + ' &middot; ' + date + ' &middot; ' + (e.count || 0) + ' measurements</div>';
                html += '<div class="small text-muted">' + camera + '</div>';
                html += '</div>';
                html += '<button type="button" class="btn btn-sm btn-outline-danger ms-2" onclick="event.stopPropagation(); sessionsDelete(\'' + escapeHtml(e.id) + '\')">Delete</button>';
                html += '</div>';
            }
            listEl.innerHTML = html;
        })
        .catch(function(err) {
            console.error('[sessions] Failed to load sessions list:', err);
            if (loadingEl) loadingEl.style.display = 'none';
            if (listEl) listEl.innerHTML = '<div class="text-danger small">Failed to load sessions. <a href="#" onclick="sessionsLoad();return false;">Retry</a></div>';
        });
}

function sessionsClearAll() {
    if (!confirm('Delete ALL sessions? This cannot be undone.')) return;
    fetch('/api/sessions', { method: 'DELETE' })
        .then(function(r) { return r.ok ? r.json() : null; })
        .then(function(data) {
            if (data && data.success) {
                sessionsLoad();
            } else {
                alert('Clear all failed.');
            }
        })
        .catch(function() { alert('Clear all failed.'); });
}

function sessionsDelete(id) {
    if (!confirm('Delete session ' + id + '? This cannot be undone.')) return;
    fetch('/api/sessions/' + encodeURIComponent(id), { method: 'DELETE' })
        .then(function(r) { return r.ok ? r.json() : null; })
        .then(function(data) {
            if (data && data.success) {
                sessionsLoad();
            } else {
                alert('Failed to delete session.');
            }
        });
}

// ============================================================================
// Detail view
// ============================================================================

function sessionsLoadDetail(id) {
    sessionDestroyAllCharts();
    var headerEl  = document.getElementById('session-detail-header');
    var measEl    = document.getElementById('session-measurements');
    if (headerEl) headerEl.innerHTML = '<div class="text-muted small">Loading\u2026</div>';
    if (measEl)   measEl.innerHTML = '';
    var summaryEl = document.getElementById('summary-table-container');
    if (summaryEl) summaryEl.style.display = 'none';

    Promise.all([
        fetch('/api/sessions/' + encodeURIComponent(id)).then(function(r) { return r.ok ? r.json() : null; }),
        fetch('/api/sessions').then(function(r) { return r.ok ? r.json() : null; }),
        (typeof padEnsureMaterialSymbols === 'function') ? padEnsureMaterialSymbols() : Promise.resolve(),
        fetch('/api/config').then(function(r) { return r.ok ? r.json() : null; }).catch(function() { return null; })
    ])
        .then(function(results) {
            var data = results[0];
            var list = results[1] || {};
            // Extract verdict thresholds from config response.
            var configData = results[3];
            _verdictThresholds = (configData && configData.verdict_thresholds) ? configData.verdict_thresholds : null;
            // Find the manifest entry by id to get camera/notes (authoritative source).
            var entry = (list.entries || []).find(function(e) { return e.id === id; }) || {};
            if (!data) {
                if (headerEl) headerEl.innerHTML = '<div class="text-danger small">Failed to load session.</div>';
                return;
            }
            // Overlay camera and notes from the manifest entry.
            data.camera = entry.camera || '';
            data.notes  = entry.notes  || '';
            _sessionsCurrentData = data;
            _exposureImageCache = {};

            // Default sweep orientation from session-level detected travel.
            var detTravel = (data.meta || []).find(function(m) { return m.key === 'detected_travel'; });
            if (detTravel && detTravel.value === 'V') _sweepOrientation = 'vertical';
            else if (detTravel && detTravel.value === 'H') _sweepOrientation = 'horizontal';
            else _sweepOrientation = 'horizontal';

            var ms = data.measurements || [];

            // Header card
            var startDt = data.started_at ? new Date(data.started_at * 1000) : null;
            var endDt   = data.ended_at   ? new Date(data.ended_at   * 1000) : null;
            var headerHtml = '<div style="min-width:0">';
            // Row 1: Session ID | Camera (spanning width)
            var idNum = (id || '').replace(/^sess_/, '');
            headerHtml += '<div class="d-flex flex-wrap gap-2 mb-2 session-tiles-row">';
            headerHtml += '<div class="welcome-stat-card welcome-stat-card--hero" data-tile-icon="tag">'
                + '<div class="welcome-stat-label">Session</div>'
                + '<div class="welcome-stat-value">' + escapeHtml(idNum) + '</div>'
                + '</div>';
            headerHtml += '<div class="welcome-stat-card welcome-stat-card--hero welcome-stat-card--wide" data-tile-icon="photo_camera">'
                + sessionInlineField('session-inline-camera', 'Camera', data.camera || '', 'Unnamed camera', false)
                + '</div>';
            headerHtml += '</div>';
            // Row 2: Started | Ended | Meta context tiles
            var startShort = startDt ? startDt.toLocaleString() : '\u2014';
            var endShort = endDt ? endDt.toLocaleString() : '\u2014';
            var row2 = [{label:'Started',value:startShort,icon:'schedule'},{label:'Ended',value:endShort,icon:'schedule'}]
                .concat((data.meta || []).map(function(m) {
                    return {label: m.label, value: sessionFormatMetaValue(m.label, m.value), icon: m.icon || ''};
                }));
            headerHtml += '<div class="d-flex flex-wrap gap-2 mb-2 session-tiles-row">';
            row2.forEach(function(t) {
                var icoAttr = t.icon ? ' data-tile-icon="' + escapeHtml(t.icon) + '"' : '';
                headerHtml += '<div class="welcome-stat-card welcome-stat-card--sm"' + icoAttr + '>'
                    + '<div class="welcome-stat-label">' + escapeHtml(t.label) + '</div>'
                    + '<div class="welcome-stat-value">' + escapeHtml(String(t.value)) + '</div>'
                    + '</div>';
            });
            headerHtml += '</div>';
            if (headerEl) headerEl.innerHTML = headerHtml;

            // Notes section (top-level card)
            var notesEl = document.getElementById('session-notes-section');
            if (notesEl) {
                var notesHtml = '<div class="card-body">';
                notesHtml += '<h6 class="mb-3">Notes</h6>';
                notesHtml += sessionInlineField('session-inline-notes', null, data.notes || '', 'Add notes\u2026', true);
                notesHtml += '</div>';
                notesEl.innerHTML = notesHtml;
                notesEl.style.display = '';
            }

            // Deviation Spread section (top-level card)
            var devEl = document.getElementById('session-deviation-section');
            if (devEl) {
                if (ms.length > 0) {
                    var devHtml = '<div class="card-body">';
                    devHtml += '<h6 class="mb-3">Deviation spread</h6>';
                    devHtml += '<div id="session-deviation-chart" style="position:relative"></div>';
                    devHtml += '<div id="session-deviation-legend"></div>';
                    devHtml += '</div>';
                    devEl.innerHTML = devHtml;
                    devEl.style.display = '';
                } else {
                    devEl.style.display = 'none';
                }
            }

            // Measurements — sorted fast to slow (ascending duration)
            if (ms.length === 0) {
                if (measEl) measEl.innerHTML = '<div class="card mb-3"><div class="card-body text-muted small">No measurements in this session.</div></div>';
                return;
            }
            ms.sort(function(a, b) { return (a.nearest_duration_ms || 0) - (b.nearest_duration_ms || 0); });
            _annotateMeasurementEdges(ms);  // memoize edges once for downstream use
            sessionBuildSummaryTable(ms);

            // Build speed groups (ms is already sorted, same-speed entries are adjacent).
            var grouped = _groupBySpeed(ms);
            var groupOrder = grouped.order;
            var groupMap   = grouped.groups;

            // Build HTML for all groups. Speed-group CSS lives in portal-custom.css.
            var measHtml = '';

            for (var gi = 0; gi < groupOrder.length; gi++) {
                var g = groupMap[groupOrder[gi]];
                var isCollapsed = false;
                var gIndices = g.indices;

                // Group stats: mean deviation, spread, curtain edge timing.
                var sumDev = 0, sumDevStops = 0, sumDevStopsN = 0;
                var sumDur = 0, minDur = Infinity, maxDur = -Infinity;
                var c1Sum = 0, c1N = 0, c2Sum = 0, c2N = 0;
                var deltaSum = 0, deltaN = 0;
                for (var k = 0; k < gIndices.length; k++) {
                    var gm = ms[gIndices[k]];
                    if (typeof gm.deviation_pct === 'number') sumDev += gm.deviation_pct;
                    if (typeof gm.deviation_stops === 'number') { sumDevStops += gm.deviation_stops; sumDevStopsN++; }
                    if (typeof gm.avg_duration_ms === 'number') {
                        sumDur += gm.avg_duration_ms;
                        if (gm.avg_duration_ms < minDur) minDur = gm.avg_duration_ms;
                        if (gm.avg_duration_ms > maxDur) maxDur = gm.avg_duration_ms;
                    }
                    var cr = _mCurtainRatioRange(gm);
                    if (cr) { deltaSum += (cr.max - cr.min) * 100; deltaN++; }
                    var gmSensors = gm.sensors || [];
                    for (var gsi = 0; gsi < gmSensors.length; gsi++) {
                        var ged = gmSensors[gsi]._edges;
                        if (!ged) continue;
                        // Skip sensors whose curtain stats were flagged invalid by
                        // the device (full-open mode or sensor recovery tail) —
                        // c1/c2 are not curtain transit times in that regime.
                        if (ged.curtainRatio === null) continue;
                        if (typeof ged.curtain1Ms === 'number') { c1Sum += ged.curtain1Ms; c1N++; }
                        if (typeof ged.curtain2Ms === 'number') { c2Sum += ged.curtain2Ms; c2N++; }
                    }
                }
                // Multi-metric verdict via shared helper (no render-order coupling).
                var gMeasurements = gIndices.map(function(idx) { return ms[idx]; });
                var groupVerdict = _computeGroupVerdict(gMeasurements, g.nominal_ms);
                var n = gIndices.length;
                var meanDev = sumDev / n;
                var meanDevStops = sumDevStopsN > 0 ? (sumDevStops / sumDevStopsN) : null;
                var devStr = meanDevStops !== null
                    ? _signPrefix(meanDevStops) + meanDevStops.toFixed(2) + ' stops (' + _signPrefix(meanDev) + meanDev.toFixed(1) + '%)'
                    : _signPrefix(meanDev) + meanDev.toFixed(1) + '%';
                var exposureStr = '';
                if (sumDur > 0) {
                    exposureStr = ' \u00b7 exposure ' + (sumDur / n).toFixed(2) + ' ms';
                }
                var spreadStr = '';
                if (n >= 2 && sumDur > 0) {
                    var meanDur = sumDur / n;
                    spreadStr = ' \u00b7 shot spread \u00b1' + ((maxDur - minDur) / meanDur * 100).toFixed(1) + '%';
                }
                var balanceStr = '';
                if (c1N > 0 && c2N > 0) {
                    var openMs  = c1Sum / c1N;
                    var closeMs = c2Sum / c2N;
                    if (openMs > 0) {
                        var bal = closeMs / openMs;
                        balanceStr = ' \u00b7 curtain balance ' + bal.toFixed(2);
                        if (deltaN > 0) {
                            var avgDelta = deltaSum / deltaN;
                            if (avgDelta >= 0.5) balanceStr += ' (\u0394' + Math.round(avgDelta) + '%)';
                        }
                    }
                }

                measHtml += '<div class="speed-group' + (isCollapsed ? ' speed-group--collapsed' : '') + '"'
                    + ' id="speed-group-' + gi + '">';
                measHtml += '<div class="speed-group-header" onclick="sessionToggleSpeedGroup(' + gi + ')">';
                measHtml += '<span class="speed-group-chevron"></span>';
                measHtml += '<span class="fw-semibold">' + escapeHtml(_formatSpeedLabel(g.label)) + '</span>';
                measHtml += sessionVerdictBadge(groupVerdict);
                measHtml += '<span class="text-muted">' + n + ' shot' + (n !== 1 ? 's' : '') + '</span>';
                measHtml += '<span class="text-muted">' + escapeHtml(exposureStr) + ' \u00b7 deviation ' + escapeHtml(devStr) + escapeHtml(spreadStr) + escapeHtml(balanceStr) + '</span>';
                measHtml += '</div>';
                measHtml += '<div class="speed-group-body">';
                for (var k = 0; k < gIndices.length; k++) {
                    measHtml += sessionMeasurementCard(ms[gIndices[k]], gIndices[k]);
                }
                measHtml += '</div></div>';
            }

            if (measEl) {
                measEl.innerHTML = measHtml;
                // Create waveform charts for all measurements
                for (var i = 0; i < ms.length; i++) {
                    sessionCreateWaveformCharts(ms[i], i);
                    renderExposureSim(data, ms[i], i);
                }
                // Create deviation scatter in header
                sessionCreateDeviationChart('session-deviation-chart', ms);
            }
        })
        .catch(function(err) {
            console.error('[sessions] Failed to load session detail:', err);
            if (headerEl) headerEl.innerHTML = '<div class="text-danger small">Failed to load session.</div>';
        });
}

function sessionSaveMetadata() {
    var id = _sessionsCurrentId;
    if (!id) return;
    // Always read from _sessionsCurrentData, which sessionInlineSave() keeps up to date.
    // This avoids persisting an open draft in the other field when only one field is being saved.
    var camera = _sessionsCurrentData ? (_sessionsCurrentData.camera || '') : '';
    var notes  = _sessionsCurrentData ? (_sessionsCurrentData.notes  || '') : '';
    fetch('/api/sessions/' + encodeURIComponent(id), {
        method: 'PATCH',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ camera: camera, notes: notes })
    })
    .then(function(r) { return r.ok ? r.json() : null; })
    .then(function(data) {
        if (data && data.success) {
            showMessage('Session saved.', 'success');
        } else {
            showMessage('Failed to save session.', 'error');
        }
    })
    .catch(function() { showMessage('Failed to save session.', 'error'); });
}

// Render an inline-editable label+value row.
// fieldId: base id prefix; label: 'Camera'/'Notes'; value: current value;
// placeholder: shown when empty; multiline: use textarea.
function sessionInlineField(fieldId, label, value, placeholder, multiline) {
    var display = value
        ? (multiline ? escapeHtml(value).replace(/\n/g, '<br>') : escapeHtml(value))
        : ('<em class="text-muted">' + escapeHtml(placeholder) + '</em>');
    var escaped = escapeHtml(value);
    var editIcon = '<button type="button" class="sif-edit-btn" title="Edit ' + escapeHtml(label || '') + '" '
        + 'onclick="sessionInlineEdit(\''+fieldId+'\')" tabindex="0">✏️</button>';
    var saveBtn   = '<button type="button" class="sif-action-btn sif-save-btn"  title="Save"   onclick="sessionInlineSave(\''+fieldId+'\')">✓ Save</button>';
    var cancelBtn = '<button type="button" class="sif-action-btn sif-cancel-btn" title="Cancel" onclick="sessionInlineCancel(\''+fieldId+'\')">✕ Cancel</button>';
    var html = '<div class="sif-row" id="' + fieldId + '">';
    if (label) html += '<span class="sif-label small text-muted">' + escapeHtml(label) + '</span>';
    html += '<div class="sif-view" onclick="sessionInlineEdit(\''+fieldId+'\')">'; 
    html += '<span class="sif-display">' + display + '</span> ' + editIcon;
    html += '</div>';
    html += '<div class="sif-edit" style="display:none">';
    if (multiline) {
        html += '<textarea id="' + fieldId + '-input" class="form-control form-control-sm sif-input" rows="3" '
            + 'placeholder="' + escapeHtml(placeholder) + '" '
            + 'onkeydown="if(event.key===\'Escape\')sessionInlineCancel(\''+fieldId+'\')" >'
            + escaped + '</textarea>';
    } else {
        html += '<input type="text" id="' + fieldId + '-input" class="form-control form-control-sm sif-input" '
            + 'value="' + escaped + '" placeholder="' + escapeHtml(placeholder) + '" '
            + 'onkeydown="if(event.key===\'Enter\')sessionInlineSave(\''+fieldId+'\');if(event.key===\'Escape\')sessionInlineCancel(\''+fieldId+'\')" />';
    }
    html += '<div class="sif-actions">' + saveBtn + cancelBtn + '</div>';
    html += '</div>';
    html += '</div>';
    return html;
}

function sessionInlineEdit(fieldId) {
    var row = document.getElementById(fieldId);
    if (!row) return;
    row.querySelector('.sif-view').style.display = 'none';
    var editDiv = row.querySelector('.sif-edit');
    editDiv.style.display = '';
    var input = document.getElementById(fieldId + '-input');
    if (input) { input.focus(); if (input.select) input.select(); }
}

function sessionInlineCancel(fieldId) {
    var row = document.getElementById(fieldId);
    if (!row) return;
    // Restore input to last saved value before hiding
    var input = document.getElementById(fieldId + '-input');
    if (input && _sessionsCurrentData) {
        var key = fieldId === 'session-inline-camera' ? 'camera' : 'notes';
        input.value = _sessionsCurrentData[key] || '';
    }
    row.querySelector('.sif-edit').style.display = 'none';
    row.querySelector('.sif-view').style.display = '';
}

function sessionInlineSave(fieldId) {
    var row = document.getElementById(fieldId);
    if (!row) return;
    var input = document.getElementById(fieldId + '-input');
    var val = input ? input.value.trim() : '';
    var key = fieldId === 'session-inline-camera' ? 'camera' : 'notes';
    if (_sessionsCurrentData) _sessionsCurrentData[key] = val;
    var displayEl = row.querySelector('.sif-display');
    if (displayEl) {
        var placeholder = fieldId === 'session-inline-camera' ? 'Unnamed camera' : 'Add notes\u2026';
        displayEl.innerHTML = val ? escapeHtml(val) : '<em class="text-muted">' + escapeHtml(placeholder) + '</em>';
    }
    row.querySelector('.sif-edit').style.display = 'none';
    row.querySelector('.sif-view').style.display = '';
    sessionSaveMetadata();
}

// ============================================================================
// Speed-group collapse toggle
// ============================================================================

function sessionToggleSpeedGroup(gi) {
    var el = document.getElementById('speed-group-' + gi);
    if (el) el.classList.toggle('speed-group--collapsed');
}

// ============================================================================
// Waveform helpers
// ============================================================================

// ============================================================================
// Curtain ratio helpers
// ============================================================================

// Compute min/max curtain ratio across all valid sensors in a measurement.
// Reads memoized s._edges (set by _annotateMeasurementEdges).
// Returns { min, max } or null if no sensor produced a valid ratio.
function _mCurtainRatioRange(m) {
    var sensors = m.sensors || [];
    var minR = null, maxR = null;
    for (var si = 0; si < sensors.length; si++) {
        var edge = sensors[si]._edges;
        if (edge && edge.curtainRatio !== null) {
            if (minR === null || edge.curtainRatio < minR) minR = edge.curtainRatio;
            if (maxR === null || edge.curtainRatio > maxR) maxR = edge.curtainRatio;
        }
    }
    if (minR === null) return null;
    return { min: minR, max: maxR };
}

// Format a curtain ratio range as "X.XX" (single) or "X.XX → Y.YY (Δ15%)".
// The delta is the spread (max - min) expressed as percentage points of the
// dimensionless ratio. Honest, magnitude-preserving, no diagnostic claim:
// the user judges severity in context with open/spread/capping.
function _curtainRatioRangeStr(range) {
    if (!range) return '\u2014';
    if (Math.abs(range.max - range.min) < 0.005) return range.min.toFixed(2);
    var deltaPct = Math.round((range.max - range.min) * 100);
    return range.min.toFixed(2) + ' \u2192 ' + range.max.toFixed(2)
         + ' (\u0394' + deltaPct + '%)';
}

function _buildSkewTooltip(m) {
    function line(label, leftUs, rightUs) {
        var spread = Math.abs(rightUs - leftUs).toFixed(1);
        return label + ':  ' + leftUs.toFixed(1) + ' \u00b5s (left) \u2192 '
             + rightUs.toFixed(1) + ' \u00b5s (right)  |  Spread: ' + spread + ' \u00b5s';
    }
    return line('Curtain 1', m.curtain1_skew_left_us, m.curtain1_skew_right_us)
         + '\n' + line('Curtain 2', m.curtain2_skew_left_us, m.curtain2_skew_right_us);
}

// ============================================================================
// Measurement card HTML
// ============================================================================

function sessionMeasurementCard(m, idx) {
    var verdict = m.verdict || 'pass';
    var speed = m.nearest_speed || '—';

    // Build metric rows for the stats table (label / value).
    var rows = [];
    if (typeof m.avg_duration_ms === 'number') {
        rows.push(['Exposure', m.avg_duration_ms.toFixed(2) + ' ms']);
    }
    if (typeof m.deviation_pct === 'number') {
        var devSign   = m.deviation_pct   >= 0 ? '+' : '';
        var stopsSign = (typeof m.deviation_stops === 'number' && m.deviation_stops >= 0) ? '+' : '';
        var devVal = (typeof m.deviation_stops === 'number')
            ? stopsSign + m.deviation_stops.toFixed(2) + ' stops (' + devSign + m.deviation_pct.toFixed(1) + '%)'
            : devSign + m.deviation_pct.toFixed(1) + '%';
        rows.push(['Deviation', devVal]);
    }
    if (m.sensor_count >= 2 && typeof m.spread_pct === 'number') {
        rows.push(['Sensor spread', m.spread_pct.toFixed(1) + '%']);
    }
    if (typeof m.capping_gradient_stops_per_mm === 'number' && m.capping_gradient_stops_per_mm >= 0.001) {
        // For 4-corner sessions, show 2D breakdown with correct extrapolation.
        if (typeof m.capping_gradient_x === 'number' && m.capping_gradient_x >= 0 &&
            typeof m.capping_gradient_y === 'number' && m.capping_gradient_y >= 0) {
            var capXStops = m.capping_gradient_x * _GATE_W;
            var capYStops = m.capping_gradient_y * _GATE_H;
            var frameStops2d = Math.sqrt(capXStops * capXStops + capYStops * capYStops);
            rows.push(['Frame capping', frameStops2d.toFixed(2) + ' stops (H:' + capXStops.toFixed(2) + ' V:' + capYStops.toFixed(2) + ')']);
        } else {
            var frameStops = m.capping_gradient_stops_per_mm * _FILM_DIAGONAL_MM;
            rows.push(['Frame capping', frameStops.toFixed(2) + ' stops']);
        }
    }
    var skewDiff = (typeof m.skew_differential === 'number') ? m.skew_differential
                 : (typeof m.curtain_twist === 'number') ? m.curtain_twist : null;
    var hasPerCurtainSkew = (typeof m.curtain1_skew_left_us === 'number');
    if (skewDiff !== null) {
        if (hasPerCurtainSkew) {
            var tip = _buildSkewTooltip(m);
            var skewVal = '<span title="' + escapeHtml(tip) + '">'
                       + escapeHtml(skewDiff.toFixed(2) + ' \u00b5s/mm')
                       + ' <span class="material-symbols-outlined skew-detail-icon">info</span></span>';
            rows.push(['Skew differential', skewVal, true]);
        } else {
            rows.push(['Skew differential', skewDiff.toFixed(2) + ' \u00b5s/mm']);
        }
    }
    var mCurtainRange = _mCurtainRatioRange(m);
    if (mCurtainRange) {
        rows.push(['Curtain balance', _curtainRatioRangeStr(mCurtainRange)]);
    }

    var html = '<div class="card mb-3" id="meas-card-' + idx + '">';
    html += '<div class="card-body">';

    // Two-column row: hero + metrics table on the left, simulation on the right (top-aligned).
    html += '<div class="row g-2 mb-2 align-items-start">';

    // Left: hero line + metrics table.
    html += '<div class="col-md-7">';
    html += '<div class="mb-2"><span class="fw-semibold">' + escapeHtml(_formatSpeedLabel(speed)) + ' ' + sessionVerdictBadge(verdict) + '</span></div>';
    if (rows.length > 0) {
        html += '<table class="table table-borderless mb-0 meas-stats-table">';
        html += '<tbody>';
        for (var ri = 0; ri < rows.length; ri++) {
            var valHtml = rows[ri][2] ? rows[ri][1] : escapeHtml(rows[ri][1]);
            html += '<tr>'
                + '<td class="meas-stats-label">' + escapeHtml(rows[ri][0]) + '</td>'
                + '<td class="meas-stats-value">' + valHtml + '</td>'
                + '</tr>';
        }
        html += '</tbody></table>';
    }
    html += '</div>';

    // Right: exposure simulation canvas — rendered after DOM insertion by renderExposureSim().
    html += '<div class="col-md-5 d-flex flex-column align-items-md-end align-items-start exposure-sim-wrapper">';
    html += '<span style="position:relative;display:inline-block;line-height:0">';
    html += '<canvas class="exposure-sim-canvas" id="exposure-sim-' + idx + '" width="720" height="480"></canvas>';
    html += '<button type="button" class="exposure-sim-flip" id="exposure-sim-flip-' + idx + '"'
        + ' onclick="sessionToggleSweepOrientation()"'
        + ' title="Flip sweep orientation"'
        + ' style="display:none">'
        + '<span class="material-symbols-outlined">swap_horiz</span></button>';
    html += '<button type="button" class="exposure-sim-play" id="exposure-sim-play-' + idx + '"'
        + ' onclick="sessionToggleExposureAnimation(' + idx + ')"'
        + ' title="Play curtain animation"'
        + ' style="display:none">'
        + '<span class="material-symbols-outlined">play_arrow</span></button>';
    html += '</span>';
    html += '<span class="text-muted mt-1 meas-sim-caption" id="exposure-sim-label-' + idx + '"></span>';
    html += '</div>';

    html += '</div>'; // end row

    // Waveform slots — always visible.
    var sensors = m.sensors || [];
    var hasWaveforms = false;
    for (var si = 0; si < sensors.length; si++) { if (sensors[si].valid) { hasWaveforms = true; break; } }
    if (hasWaveforms) {
        html += '<div id="wf-combined-slot-' + idx + '" style="height:160px;background:var(--bs-secondary-bg,#1a1a1a);border-radius:4px;margin-bottom:8px"></div>';
        // Individual per-sensor charts
        for (var si = 0; si < sensors.length; si++) {
            var s = sensors[si];
            if (!s.valid) continue;
            html += '<div class="mb-2">';
            var sDurStr = (typeof s.duration_ms === 'number') ? '<strong>' + s.duration_ms.toFixed(2) + ' ms</strong>' : '\u2014';
            var sDevStr = '';
            if (typeof s.duration_ms === 'number' && typeof m.nearest_duration_ms === 'number' && m.nearest_duration_ms > 0) {
                var sDev = (s.duration_ms - m.nearest_duration_ms) / m.nearest_duration_ms * 100;
                var sStops = Math.log2(s.duration_ms / m.nearest_duration_ms);
                sDevStr = ' &middot; deviation <strong>' + (sStops >= 0 ? '+' : '') + sStops.toFixed(2) + ' stops</strong> (' + (sDev >= 0 ? '+' : '') + sDev.toFixed(1) + '%)';
            }
            var sEdgeStr = '';
            var sEdge = s._edges;
            if (sEdge) {
                sEdgeStr = ' &middot; 1st curtain <strong>' + sEdge.curtain1Ms.toFixed(2) + ' ms</strong> / dwell <strong>' + sEdge.dwellMs.toFixed(2) + ' ms</strong> / 2nd curtain <strong>' + sEdge.curtain2Ms.toFixed(2) + ' ms</strong>';
                if (sEdge.curtainRatio !== null) sEdgeStr += ' &middot; curtain balance <strong>' + sEdge.curtainRatio.toFixed(2) + '</strong>';
            }
            html += '<div class="small text-muted mb-1">Sensor <strong>' + (si + 1) + '</strong> &middot; exposure ' + sDurStr + sDevStr + sEdgeStr + '</div>';
            html += '<div id="wf-slot-' + idx + '-' + si + '" style="height:120px;background:var(--bs-secondary-bg,#1a1a1a);border-radius:4px"></div>';
            // Curtain strip: 3 segments — 1st curtain | dwell | 2nd curtain.
            if (sEdge) {
                var se1L  = (sEdge.curtain1Frac    * 100).toFixed(2);
                var se1W  = ((sEdge.curtain1EndFrac - sEdge.curtain1Frac)  * 100).toFixed(2);
                var seDL  = (sEdge.dwellFrac        * 100).toFixed(2);
                var seDW  = ((sEdge.dwellEndFrac    - sEdge.dwellFrac)     * 100).toFixed(2);
                var se2L  = (sEdge.curtain2Frac     * 100).toFixed(2);
                var se2W  = ((sEdge.curtain2EndFrac - sEdge.curtain2Frac)  * 100).toFixed(2);
                var stripHtml = '<div style="position:relative;height:3px;margin-top:3px;border-radius:2px;overflow:hidden">';
                stripHtml += '<div style="position:absolute;left:' + se1L + '%;width:' + se1W + '%;height:100%;background:#FF9500;border-radius:2px" title="1st curtain edge time at this sensor: ' + sEdge.curtain1Ms.toFixed(2) + ' ms"></div>';
                if (parseFloat(seDW) > 0.1) {
                    stripHtml += '<div style="position:absolute;left:' + seDL + '%;width:' + seDW + '%;height:100%;background:#34C759" title="Dwell: ' + sEdge.dwellMs.toFixed(2) + ' ms"></div>';
                }
                stripHtml += '<div style="position:absolute;left:' + se2L + '%;width:' + se2W + '%;height:100%;background:#FF3B30;border-radius:2px" title="2nd curtain edge time at this sensor: ' + sEdge.curtain2Ms.toFixed(2) + ' ms"></div>';
                stripHtml += '</div>';
                html += stripHtml;
            }
            html += '</div>';
        }
    }

    html += '</div></div>';
    return html;
}

// ============================================================================
// Chart.js waveform charts
// ============================================================================

// Pick the valid sensor whose pulse opens earliest in the shared waveform
// window. Returns -1 when no sensor has usable timing/waveform data.
//
// Used as the single source of truth for the waveform time origin so that
// t=0 ms always corresponds to the earliest first-light event across the
// measurement, regardless of logical sensor order. Without this, sessions
// where (for example) the bottom row fires before the top row would render
// negative chart times purely because S1 happened to be picked as reference.
function _earliestValidSensorIdx(sensors) {
    if (!sensors) return -1;
    var best = -1, bestStart = Infinity;
    for (var i = 0; i < sensors.length; i++) {
        var s = sensors[i];
        if (!s || !s.valid) continue;
        if (typeof s.pulse_start_frac !== 'number') continue;
        if (typeof s.pulse_end_frac !== 'number' || !(s.pulse_end_frac > s.pulse_start_frac)) continue;
        if (typeof s.duration_ms !== 'number' || !(s.duration_ms > 0)) continue;
        if (!s.waveform || s.waveform.length < 2) continue;
        if (s.pulse_start_frac < bestStart) {
            bestStart = s.pulse_start_frac;
            best = i;
        }
    }
    return best;
}

// Compute the shared time base used by both the per-sensor and combined
// waveform charts (and mirrored into the exposure model via refPulse*).
// Returns null when no usable reference sensor exists; caller renders the
// charts in index-based mode in that case.
function _computeSharedWaveformTimeBase(sensors) {
    var idx = _earliestValidSensorIdx(sensors);
    if (idx < 0) return null;
    var ref = sensors[idx];
    var refLen = ref.waveform.length;
    var pulseFrac = ref.pulse_end_frac - ref.pulse_start_frac;
    if (!(pulseFrac > 0) || !(ref.duration_ms > 0) || refLen <= 1) return null;
    var msPerSample = ref.duration_ms / (pulseFrac * (refLen - 1));
    var exposureStartIdx = ref.pulse_start_frac * (refLen - 1);
    return {
        ref: ref,
        refIdx: idx,
        refLen: refLen,
        msPerSample: msPerSample,
        exposureStartIdx: exposureStartIdx,
        xMin: (0 - exposureStartIdx) * msPerSample,
        xMax: (refLen - 1 - exposureStartIdx) * msPerSample,
        idxToMs: function(i) { return (i - exposureStartIdx) * msPerSample; }
    };
}

function sessionCreateWaveformCharts(m, idx) {
    if (!m) return;
    _sessionEnsureHoverGuidePlugin();
    sessionCreateCombinedWaveformChart(m, idx);
    var sensors = m.sensors || [];

    // Shared time base: t=0 ms is the earliest valid sensor's pulse start,
    // not whichever sensor happens to come first in logical order.
    var tb = _computeSharedWaveformTimeBase(sensors);
    var sharedMsPerSample = tb ? tb.msPerSample : 0;
    var sharedExposureStartIdx = tb ? tb.exposureStartIdx : 0;
    var sharedHasTime = !!tb;
    var sharedXMin = tb ? tb.xMin : 0;
    var sharedXMax = tb ? tb.xMax : 0;
    function sharedIdxToMs(i) { return (i - sharedExposureStartIdx) * sharedMsPerSample; }

    for (var si = 0; si < sensors.length; si++) {
        var s = sensors[si];
        if (!s.valid) continue;
        var slot = document.getElementById('wf-slot-' + idx + '-' + si);
        if (!slot) continue;
        // slot.innerHTML is reset below when the canvas is appended; no skip guard needed.

        var wf = s.waveform;
        if (!wf || wf.length === 0) {
            slot.innerHTML = '<div style="display:flex;align-items:center;justify-content:center;height:100%;color:#555;font-size:11px">No waveform data</div>';
            continue;
        }
        if (!window.Chart) {
            slot.innerHTML = '<div style="display:flex;align-items:center;justify-content:center;height:100%;color:#555;font-size:11px">Charts require internet connection for first load</div>';
            continue;
        }

        // Raw ADC midpoint: signal drops during exposure (shutter opens → less light blocked).
        // fill:{value:baseline} fills between signal and the resting baseline, highlighting the dip.
        var baseline = s.baseline_adc;
        var minAdc   = s.min_adc;
        var midVals  = [];
        for (var pi = 0; pi < wf.length; pi++) {
            midVals.push({ x: pi, y: wf[pi] });
        }
        var len = wf.length;

        // Y range: minAdc at floor, baseline + headroom at ceiling
        var adcPad = (typeof baseline === 'number' && typeof minAdc === 'number')
            ? Math.round((baseline - minAdc) * 0.08) + 30 : 50;
        var yMin = (typeof minAdc   === 'number') ? minAdc   - adcPad : 0;
        var yMax = (typeof baseline === 'number') ? baseline + adcPad : 4096;

        // Theme-aware annotation colors
        var isDark = document.documentElement.getAttribute('data-bs-theme') === 'dark';
        var boxBgColor     = isDark ? 'rgba(150,150,150,0.14)' : 'rgba(80,80,80,0.12)';
        var boxBorderColor = isDark ? 'rgba(150,150,150,0.40)' : 'rgba(80,80,80,0.35)';

        // Convert data points to time-based x using shared time base
        var midValsTime = [];
        for (var ti = 0; ti < wf.length; ti++) {
            midValsTime.push({ x: sharedHasTime ? sharedIdxToMs(ti) : ti, y: wf[ti] });
        }

        var annotations = {};
        if (typeof s.pulse_start_frac === 'number' && s.pulse_start_frac > 0 &&
                typeof s.pulse_end_frac === 'number' && s.pulse_end_frac > s.pulse_start_frac &&
                typeof s.duration_ms === 'number' && s.duration_ms > 0 &&
                typeof m.nearest_duration_ms === 'number' && m.nearest_duration_ms > 0) {
            var xActualStart = s.pulse_start_frac * (len - 1);
            var xActualEnd   = s.pulse_end_frac   * (len - 1);
            var actualSamples = xActualEnd - xActualStart;
            var idealSamples  = actualSamples * m.nearest_duration_ms / s.duration_ms;

            // Ideal center: same logic as timing strip — dwell midpoint > waveform argmin > actual midpoint.
            var pwIdealCenterFrac;
            var pwEdge = s._edges;
            if (pwEdge && pwEdge.dwellMs > 0.05) {
                pwIdealCenterFrac = (pwEdge.dwellFrac + pwEdge.dwellEndFrac) / 2;
            } else {
                var pwMinV = Infinity, pwMinI = Math.round(xActualStart);
                for (var pwi = Math.round(xActualStart); pwi <= Math.round(xActualEnd) && pwi < wf.length; pwi++) {
                    if (wf[pwi] < pwMinV) { pwMinV = wf[pwi]; pwMinI = pwi; }
                }
                pwIdealCenterFrac = pwMinI / (len - 1);
            }
            var xIdealStart = pwIdealCenterFrac * (len - 1) - idealSamples / 2;
            var xIdealEnd   = pwIdealCenterFrac * (len - 1) + idealSamples / 2;
            if (xIdealEnd > xIdealStart) {
                var bxMin = sharedHasTime ? sharedIdxToMs(xIdealStart) : Math.round(xIdealStart);
                var bxMax = sharedHasTime ? sharedIdxToMs(xIdealEnd)   : Math.round(xIdealEnd);
                annotations.pulseWindow = {
                    type: 'box',
                    xMin: bxMin, xMax: bxMax,
                    yMin: yMin,  yMax: yMax,
                    backgroundColor: boxBgColor,
                    borderColor: boxBorderColor,
                    borderWidth: 1
                };
            }
        }

        var pal = _WF_SENSOR_PALETTE[si % _WF_SENSOR_PALETTE.length];
        var canvas = document.createElement('canvas');
        canvas.style.cssText = 'display:block;width:100%;height:120px';
        slot.innerHTML = '';
        slot.appendChild(canvas);

        // Measurement box + tilted line: spans pulse window, endpoints touch waveform at each crossing.
        var measureLineData = [];
        if (typeof s.pulse_start_frac === 'number' && s.pulse_start_frac > 0 &&
                typeof s.pulse_end_frac === 'number' && s.pulse_end_frac > s.pulse_start_frac) {
            var mlX1 = s.pulse_start_frac * (len - 1);
            var mlX2 = s.pulse_end_frac   * (len - 1);
            measureLineData = [
                { x: sharedHasTime ? sharedIdxToMs(mlX1) : Math.round(mlX1), y: _wfMidpoint(wf, Math.round(mlX1)) },
                { x: sharedHasTime ? sharedIdxToMs(mlX2) : Math.round(mlX2), y: _wfMidpoint(wf, Math.round(mlX2)) }
            ];
        }

        var sXAxisCfg = { type: 'linear', min: sharedXMin, max: sharedXMax };
        if (sharedHasTime) {
            sXAxisCfg.display = true;
            sXAxisCfg.border = { display: false };
            sXAxisCfg.ticks = { font: { size: 9 }, maxTicksLimit: 8, padding: 2, callback: function(v) { return v.toFixed(1) + ' ms'; } };
            sXAxisCfg.grid  = { color: isDark ? 'rgba(255,255,255,0.08)' : 'rgba(0,0,0,0.06)', tickLength: 0 };
        } else {
            sXAxisCfg.display = false;
        }

        var chartKey = 'wf-' + idx + '-' + si;
        if (_sessionCharts[chartKey]) { _sessionCharts[chartKey].destroy(); }
        _sessionCharts[chartKey] = new Chart(canvas, {
            type: 'line',
            data: {
                datasets: [{
                    data: midValsTime,
                    borderColor: pal.faint,
                    borderWidth: 1.5,
                    pointRadius: 0,
                    fill: false,
                    tension: 0
                }, {
                    data: measureLineData,
                    borderColor: 'rgba(220,50,50,0.90)',
                    borderWidth: 2,
                    pointRadius: 3,
                    pointBackgroundColor: 'rgba(220,50,50,0.90)',
                    fill: false,
                    tension: 0
                }]
            },
            options: {
                animation: false,
                responsive: true,
                maintainAspectRatio: false,
                plugins: {
                    legend:     { display: false },
                    tooltip:    { enabled: false },
                    annotation: { annotations: annotations },
                    sessionHoverGuide: {
                        enabled: true,
                        measurementIdx: idx,
                        unit: sharedHasTime ? 'ms' : '',
                        decimals: sharedHasTime ? 2 : 1,
                        lineColor: isDark ? 'rgba(255,255,255,0.45)' : 'rgba(0,0,0,0.35)',
                        labelBackgroundColor: isDark ? 'rgba(25,25,25,0.86)' : 'rgba(255,255,255,0.92)',
                        labelColor: isDark ? '#f5f5f5' : '#222'
                    }
                },
                scales: {
                    x: sXAxisCfg,
                    y: { type: 'linear', display: false, min: yMin, max: yMax }
                }
            }
        });
    }
}

// ============================================================================
// Exposure simulation — simulated gray card showing capping
// ============================================================================

// Film gate dimensions and exposure exaggeration shared by static + animation.
var _EXPOSURE_GATE_W = _GATE_W;
var _EXPOSURE_GATE_H = _GATE_H;
var _EXPOSURE_EXAGGERATE = 4.0;
var _EXPOSURE_ANIM_MS = 2000; // sweep duration
var _EXPOSURE_HOLD_MS = 1000; // settle hold after sweep

// Build the resolved model used by both the static renderer and the animation.
// Returns null when the canvas should be hidden (insufficient data).
function _buildExposureSimModel(data, m) {
    var sensors = m.sensors || [];
    var pts = []; // { x, y, dur, idx }
    var meta = (data && data.meta) || [];

    // Read per-sensor positions from session meta (new format).
    var positions = [];
    var posEntry = null;
    for (var mi = 0; mi < meta.length; mi++) {
        if (meta[mi].key === 'sensor_positions' && meta[mi].positions) {
            posEntry = meta[mi].positions;
            break;
        }
    }
    if (posEntry) {
        for (var pi = 0; pi < posEntry.length; pi++) {
            positions.push({ x: posEntry[pi].x_mm, y: posEntry[pi].y_mm });
        }
    } else {
        // Fallback for old sessions: reconstruct from legacy offset
        var offsetX = 0, offsetY = 0;
        for (var mi = 0; mi < meta.length; mi++) {
            if (meta[mi].key === 'sensor_offset') {
                offsetX = meta[mi].sensor_offset_x_mm || 0;
                offsetY = meta[mi].sensor_offset_y_mm || 0;
                break;
            }
        }
        if (offsetX <= 0 || offsetY <= 0) return null;
        positions = [
            { x: -offsetX, y: -offsetY },
            { x: 0,        y: 0        },
            { x:  offsetX, y:  offsetY }
        ];
    }
    for (var si = 0; si < sensors.length && si < positions.length; si++) {
        if (sensors[si].valid && typeof sensors[si].duration_ms === 'number') {
            pts.push({ x: positions[si].x, y: positions[si].y, dur: sensors[si].duration_ms, idx: si });
        }
    }
    if (pts.length < 2) return null;

    // Travel axis is driven entirely by the user-selected sweep orientation.
    var travelAxis = (_sweepOrientation === 'vertical') ? 'y' : 'x';

    var minDur = Infinity, maxDur = -Infinity;
    for (var i = 0; i < pts.length; i++) {
        if (pts[i].dur < minDur) minDur = pts[i].dur;
        if (pts[i].dur > maxDur) maxDur = pts[i].dur;
    }
    // Geometric mean of min/max — stop-space neutral so symmetric overexposure
    // and underexposure cancel (arithmetic mean is biased toward longer
    // exposures and shifts the visual reference off-center in stops).
    var avgDur = Math.sqrt(minDur * maxDur);
    var rangeStops = Math.abs(Math.log2(maxDur / minDur));

    pts.sort(function(a, b) { return (travelAxis === 'x') ? (a.x - b.x) : (a.y - b.y); });

    // Reference sensor for ms↔frac conversion (used by hover scrubbing).
    // All valid sensors share the same waveform window, so any one works for
    // the frac math — but we pick the *earliest-opening* sensor so the
    // animation's t=0 lines up with the waveform charts' t=0 (which use the
    // same earliest-sensor reference). Without this, ms↔frac conversions
    // would silently drift between charts and animation when the first
    // logical sensor isn't the one that opens first.
    var refSensor = null;
    var refIdx = _earliestValidSensorIdx(sensors);
    if (refIdx >= 0) refSensor = sensors[refIdx];
    return {
        pts: pts,
        sensors: sensors,
        positions: positions,
        travelAxis: travelAxis,
        isHorizontal: travelAxis === 'x',
        avgDur: avgDur,
        rangeStops: rangeStops,
        refPulseStartFrac: refSensor ? refSensor.pulse_start_frac : 0,
        refPulseEndFrac:   refSensor ? refSensor.pulse_end_frac   : 1,
        refDurationMs:     refSensor ? refSensor.duration_ms      : 0,
        // Per-curtain per-position skew (µs). Undefined for <4 sensor sessions.
        curtain1SkewLeftUs:  m.curtain1_skew_left_us,
        curtain1SkewRightUs: m.curtain1_skew_right_us,
        curtain2SkewLeftUs:  m.curtain2_skew_left_us,
        curtain2SkewRightUs: m.curtain2_skew_right_us
    };
}

// Build an offscreen canvas with the 2D exposure fill using column-based
// bilinear interpolation that respects focal-plane shutter physics.
//
// A focal-plane curtain is a (possibly tilted) line sweeping along the travel
// axis.  This means:
//   - At any travel-axis position with a single sensor, the entire cross-axis
//     slice gets that sensor's exposure (no cross-axis info available).
//   - At a position with two sensors (e.g. top + bottom), the cross-axis
//     variation is a linear gradient between them (curtain tilt).
//   - Between travel-axis columns, we interpolate linearly along the travel
//     axis; at the gate edges we extrapolate from the nearest column(s).
//
// Works for any sensor layout: 1-sensor (flat), 3-line collinear (1D gradient,
// identical to the old CanvasGradient), 4-corner (full bilinear with tilt),
// L-shape (mixed 1-point and 2-point columns).
//
// Returns null when per-sensor variation is below the rendering threshold.
function _buildExposureImage(model, W, H) {
    if (!model || model.rangeStops < 0.001) return null;
    var N = model.pts.length;
    if (N < 2) return null;

    var isH = model.isHorizontal;
    var logAvgDur = Math.log2(model.avgDur);

    // --- Step 1: group sensors into columns along the travel axis ----------
    // A "column" is a set of sensors sharing the same travel-axis coordinate
    // (within 0.5 mm tolerance to handle float imprecision).
    var COL_TOL = 0.5; // mm
    var cols = []; // [{ tPos, sensors: [{ cPos, logDur }] }]
    // Sort pts by travel-axis position first.
    var sorted = model.pts.slice().sort(function(a, b) {
        return isH ? (a.x - b.x) : (a.y - b.y);
    });
    for (var i = 0; i < sorted.length; i++) {
        var tPos = isH ? sorted[i].x : sorted[i].y;
        var cPos = isH ? sorted[i].y : sorted[i].x;
        var logDur = Math.log2(sorted[i].dur);
        // Merge into existing column if close enough.
        var merged = false;
        if (cols.length > 0) {
            var last = cols[cols.length - 1];
            if (Math.abs(tPos - last.tPos) < COL_TOL) {
                last.sensors.push({ cPos: cPos, logDur: logDur });
                merged = true;
            }
        }
        if (!merged) {
            cols.push({ tPos: tPos, sensors: [{ cPos: cPos, logDur: logDur }] });
        }
    }
    // Sort sensors within each column by cross-axis position.
    for (var ci = 0; ci < cols.length; ci++) {
        cols[ci].sensors.sort(function(a, b) { return a.cPos - b.cPos; });
    }

    // --- Step 2: helper to evaluate a single column at a cross-axis pos ----
    // Linear interpolation/extrapolation within the column's sensor list.
    // Single-sensor columns return a constant (uniform cross-axis exposure).
    function evalColumn(col, cMm) {
        var s = col.sensors;
        if (s.length === 1) return s[0].logDur;
        // Clamp-extrapolate: beyond the outermost sensors, hold the edge
        // gradient (linear extrapolation from the two nearest sensors).
        if (cMm <= s[0].cPos) {
            if (s.length === 1) return s[0].logDur;
            var slope = (s[1].logDur - s[0].logDur) / (s[1].cPos - s[0].cPos);
            return s[0].logDur + slope * (cMm - s[0].cPos);
        }
        if (cMm >= s[s.length - 1].cPos) {
            var n = s.length;
            var slope2 = (s[n - 1].logDur - s[n - 2].logDur) / (s[n - 1].cPos - s[n - 2].cPos);
            return s[n - 1].logDur + slope2 * (cMm - s[n - 1].cPos);
        }
        // Interior: piecewise linear between adjacent sensors.
        for (var j = 0; j < s.length - 1; j++) {
            if (cMm >= s[j].cPos && cMm <= s[j + 1].cPos) {
                var span = s[j + 1].cPos - s[j].cPos;
                if (span < 1e-9) return s[j].logDur;
                var t = (cMm - s[j].cPos) / span;
                return s[j].logDur + t * (s[j + 1].logDur - s[j].logDur);
            }
        }
        return s[s.length - 1].logDur;
    }

    // --- Step 3: for each pixel, interpolate between adjacent columns ------
    var offscreen = document.createElement('canvas');
    offscreen.width = W;
    offscreen.height = H;
    var octx = offscreen.getContext('2d');
    var imgData = octx.createImageData(W, H);
    var pixels = imgData.data;

    var halfT = isH ? _EXPOSURE_GATE_W / 2 : _EXPOSURE_GATE_H / 2;
    var halfC = isH ? _EXPOSURE_GATE_H / 2 : _EXPOSURE_GATE_W / 2;
    var tScale = (2 * halfT) / (((isH ? W : H)) - 1);
    var cScale = (2 * halfC) / (((isH ? H : W)) - 1);

    for (var py = 0; py < H; py++) {
        for (var px = 0; px < W; px++) {
            // Map pixel to mm.  Travel axis = x for horizontal, y for vertical.
            // Y is flipped (py=0 = canvas top = physical top of gate = +halfH mm),
            // so the cross-axis (horizontal sweep) and travel-axis (vertical sweep)
            // Y conversions both subtract py from halfC/halfT instead of adding.
            var tMm, cMm;
            if (isH) {
                tMm = -halfT + px * tScale;
                cMm = halfC - py * cScale;
            } else {
                tMm = halfT - py * tScale;
                cMm = -halfC + px * cScale;
            }

            // Find the two columns bracketing tMm.
            var logDurHere;
            if (cols.length === 1) {
                logDurHere = evalColumn(cols[0], cMm);
            } else if (tMm <= cols[0].tPos) {
                // Extrapolate before first column using first two columns.
                var v0 = evalColumn(cols[0], cMm);
                var v1 = evalColumn(cols[1], cMm);
                var span = cols[1].tPos - cols[0].tPos;
                var slope = (span > 1e-9) ? (v1 - v0) / span : 0;
                logDurHere = v0 + slope * (tMm - cols[0].tPos);
            } else if (tMm >= cols[cols.length - 1].tPos) {
                // Extrapolate after last column.
                var n = cols.length;
                var vA = evalColumn(cols[n - 2], cMm);
                var vB = evalColumn(cols[n - 1], cMm);
                var span2 = cols[n - 1].tPos - cols[n - 2].tPos;
                var slope2 = (span2 > 1e-9) ? (vB - vA) / span2 : 0;
                logDurHere = vB + slope2 * (tMm - cols[n - 1].tPos);
            } else {
                // Interior: linear blend between adjacent columns.
                for (var ci = 0; ci < cols.length - 1; ci++) {
                    if (tMm >= cols[ci].tPos && tMm <= cols[ci + 1].tPos) {
                        var vL = evalColumn(cols[ci], cMm);
                        var vR = evalColumn(cols[ci + 1], cMm);
                        var span3 = cols[ci + 1].tPos - cols[ci].tPos;
                        var t = (span3 > 1e-9) ? (tMm - cols[ci].tPos) / span3 : 0;
                        logDurHere = vL + t * (vR - vL);
                        break;
                    }
                }
            }

            var devStops = (logDurHere - logAvgDur) * _EXPOSURE_EXAGGERATE;
            var b = 128 + devStops * 40;
            if (b < 30) b = 30; else if (b > 225) b = 225;
            b = b | 0;
            var off = (py * W + px) << 2;
            pixels[off]     = b;
            pixels[off + 1] = b;
            pixels[off + 2] = b;
            pixels[off + 3] = 255;
        }
    }
    octx.putImageData(imgData, 0, 0);
    return offscreen;
}

// Cached wrapper: returns a previously built image or builds and caches one.
function _getOrBuildExposureImage(model, W, H, idx) {
    var key = idx + '_' + _sweepOrientation + '_' + W + 'x' + H;
    if (_exposureImageCache[key]) return _exposureImageCache[key];
    var img = _buildExposureImage(model, W, H);
    if (img) _exposureImageCache[key] = img;
    return img;
}

// Draw the red sensor position markers with per-sensor stops deviation labels.
function _drawExposureSensorMarkers(ctx, model, W, H) {
    // Compute geometric mean of all sensor durations for deviation reference.
    var logSum = 0;
    for (var i = 0; i < model.pts.length; i++) logSum += Math.log(model.pts[i].dur);
    var geoMean = Math.exp(logSum / model.pts.length);

    for (var i = 0; i < model.pts.length; i++) {
        var px = ((model.pts[i].x + _EXPOSURE_GATE_W / 2) / _EXPOSURE_GATE_W) * W;
        // Y axis is flipped: positive y_mm (physical top of gate) maps to
        // small py (top of canvas), matching real-world orientation.
        var py = ((_EXPOSURE_GATE_H / 2 - model.pts[i].y) / _EXPOSURE_GATE_H) * H;
        ctx.beginPath();
        ctx.arc(px, py, 8, 0, 2 * Math.PI);
        ctx.strokeStyle = 'rgba(255,100,100,0.9)';
        ctx.lineWidth = 3;
        ctx.stroke();

        // Deviation label (stops from geometric mean).
        // Position inside the sensor field: below marker for top-half sensors,
        // above marker for bottom-half sensors. Works for any sensor layout.
        var devStops = Math.log2(model.pts[i].dur / geoMean);
        var label = (devStops >= 0 ? '+' : '\u2212') + Math.abs(devStops).toFixed(2);
        ctx.font = 'bold 22px monospace';
        ctx.textAlign = 'center';
        var labelBelow = (py < H / 2);
        ctx.textBaseline = labelBelow ? 'top' : 'bottom';
        var ly = labelBelow ? py + 12 : py - 12;
        ctx.strokeStyle = '#000';
        ctx.lineWidth = 4;
        ctx.lineJoin = 'round';
        ctx.strokeText(label, px, ly);
        ctx.fillStyle = '#fff';
        ctx.fillText(label, px, ly);
    }
}

function renderExposureSim(data, m, idx) {
    var canvas = document.getElementById('exposure-sim-' + idx);
    var label  = document.getElementById('exposure-sim-label-' + idx);
    var playBtn = document.getElementById('exposure-sim-play-' + idx);
    var flipBtn = document.getElementById('exposure-sim-flip-' + idx);
    if (!canvas) return;
    var ctx = canvas.getContext('2d');
    var W = canvas.width, H = canvas.height;

    var model = _buildExposureSimModel(data, m);
    if (!model) {
        canvas.style.display = 'none';
        if (label) label.textContent = '';
        if (playBtn) playBtn.style.display = 'none';
        if (flipBtn) flipBtn.style.display = 'none';
        return;
    }
    canvas.style.display = '';

    var exposureImg = _getOrBuildExposureImage(model, W, H, idx);
    if (exposureImg) {
        ctx.drawImage(exposureImg, 0, 0);
    } else {
        ctx.fillStyle = '#808080';
        ctx.fillRect(0, 0, W, H);
    }
    _drawExposureSensorMarkers(ctx, model, W, H);

    if (label) {
        // Cyan ruler in the animation = linear-fit \"perfect shutter\" slit
        // width, drawn for visual comparison against the measured slit.
        label.textContent = 'Approx. exposure & slit simulation \u00b7 exposure differences exaggerated \u00d7'
            + _EXPOSURE_EXAGGERATE
            + ' \u00b7 cyan ruler = ideal slit (linear model)';
    }

    // Play button — always available; the user picks the shutter type.
    if (playBtn) {
        playBtn.style.display = '';
        playBtn.innerHTML = '<span class="material-symbols-outlined">play_arrow</span>';
        playBtn.title = 'Play curtain animation';
    }

    // Flip button — toggles sweep orientation session-wide. Icon and tooltip
    // reflect the current orientation; clicking switches to the other.
    if (flipBtn) {
        flipBtn.style.display = '';
        if (_sweepOrientation === 'vertical') {
            flipBtn.innerHTML = '<span class="material-symbols-outlined">swap_vert</span>';
            flipBtn.title = 'Sweep: vertical \u2014 click to switch to horizontal';
        } else {
            flipBtn.innerHTML = '<span class="material-symbols-outlined">swap_horiz</span>';
            flipBtn.title = 'Sweep: horizontal \u2014 click to switch to vertical';
        }
    }
}

// ----------------------------------------------------------------------------
// Curtain sweep animation (overlay on the exposure-sim canvas)
// ----------------------------------------------------------------------------

// Build per-curtain interpolation tables along the travel axis.
// Returns { c1: [{posMm, frac},...], c2: [...], winStart, winEnd } or null
// when curtain_stats is unavailable for any sensor (caller falls back).
//
// Each curtain is modelled as constant velocity across the gate, fit by
// least-squares linear regression on (frac, posMm) sensor crossings. With
// only 3 sensors lying on a single line, piecewise interpolation produces
// segmentation artifacts (slit appearing to grow/shrink mid-sweep when the
// c1 and c2 segment boundaries are out of phase). A single linear fit per
// curtain is honest about the resolution we have and yields a slit width
// that varies monotonically across the sweep — consistent with the dwell
// trend observed at the sensors.
//
// winStart/winEnd are extrapolated so the slit enters from off-frame and
// exits past the opposite edge.
function _buildExposureCurtainTimeline(model) {
    var rawC1 = [], rawC2 = [];
    for (var i = 0; i < model.pts.length; i++) {
        var p = model.pts[i];
        var s = model.sensors[p.idx];
        var cs = s && s.curtain_stats;
        if (!cs) return null;
        // Prefer the 50% (steepest-edge) crossing for geometric accuracy.
        // Fall back to start/end for sessions captured before mid_frac existed.
        var c1Frac = (typeof cs.curtain1_mid_frac === 'number')
                     ? cs.curtain1_mid_frac : cs.curtain1_start_frac;
        var c2Frac = (typeof cs.curtain2_mid_frac === 'number')
                     ? cs.curtain2_mid_frac : cs.curtain2_end_frac;
        if (typeof c1Frac !== 'number' || typeof c2Frac !== 'number') return null;
        var posMm = model.isHorizontal ? p.x : p.y;
        rawC1.push({ posMm: posMm, frac: c1Frac });
        rawC2.push({ posMm: posMm, frac: c2Frac });
    }
    if (rawC1.length < 2) return null;

    // Linear regression posMm = a + b*frac. Returns {slope, intercept}.
    function fit(pts) {
        var n = pts.length, sx = 0, sy = 0;
        for (var i = 0; i < n; i++) { sx += pts[i].frac; sy += pts[i].posMm; }
        var mx = sx / n, my = sy / n, num = 0, den = 0;
        for (var j = 0; j < n; j++) {
            var dx = pts[j].frac - mx, dy = pts[j].posMm - my;
            num += dx * dy; den += dx * dx;
        }
        var slope = (den > 1e-12) ? (num / den) : 0;
        return { slope: slope, intercept: my - slope * mx };
    }
    var f1 = fit(rawC1), f2 = fit(rawC2);

    // Helper: build a 2-point table at the inner sensor frac span so the
    // existing _interpCurtainPos extrapolates linearly outside it.
    //
    // Note: a 3-sensor quadratic (Lagrange) fit was prototyped here. It
    // interpolates the sensors exactly and visually shows curtain
    // acceleration *within* the sensor span, but the linear extrapolation
    // outside that span uses the local quadratic tangent — which on an
    // accelerating curtain is the *fast* end at exit and the *slow* end at
    // entry. That asymmetric extrapolation produces an artificial slit
    // widening during the entry/exit animation phases (much larger than the
    // real ~6% growth across the gate). The residual ticks already convey
    // the acceleration diagnostic honestly, so the visualization stays on
    // the linear mean curtain to keep the animation faithful.
    function tableFrom(fit, raw) {
        raw.sort(function(a, b) { return a.frac - b.frac; });
        var fA = raw[0].frac, fB = raw[raw.length - 1].frac;
        return [
            { frac: fA, posMm: fit.intercept + fit.slope * fA },
            { frac: fB, posMm: fit.intercept + fit.slope * fB }
        ];
    }
    var c1 = tableFrom(f1, rawC1);
    var c2 = tableFrom(f2, rawC2);

    // Sweep direction from c1 slope (mm per frac unit).
    var axisMin = model.isHorizontal ? -_EXPOSURE_GATE_W / 2 : -_EXPOSURE_GATE_H / 2;
    var axisMax = -axisMin;
    var entryMm = (f1.slope >= 0) ? axisMin : axisMax;
    var exitMm  = (f1.slope >= 0) ? axisMax : axisMin;

    // Extrapolate window endpoints: c1 reaches entry edge at winStart;
    // c2 reaches exit edge at winEnd.
    var winStart = (f1.slope !== 0) ? (entryMm - f1.intercept) / f1.slope : c1[0].frac;
    var winEnd   = (f2.slope !== 0) ? (exitMm  - f2.intercept) / f2.slope : c2[c2.length - 1].frac;
    if (!isFinite(winStart)) winStart = c1[0].frac;
    if (!isFinite(winEnd))   winEnd   = c2[c2.length - 1].frac;

    // Guard: window encloses sensor-derived range.
    winStart = Math.min(winStart, c1[0].frac);
    winEnd   = Math.max(winEnd,   c2[c2.length - 1].frac);

    var tl = { c1: c1, c2: c2, winStart: winStart, winEnd: winEnd };
    tl.meanSlitMm = _computeMeanSlitMm(tl);
    return tl;
}

// Synthesize a uniform-speed curtain timeline when curtain_stats is missing.
// Curtains traverse the gate over a fixed fraction of the timeline, with the
// dwell between them sized so that the per-sensor exposure roughly matches.
function _synthesizeExposureCurtainTimeline(model) {
    var n = model.pts.length;
    if (n < 2) return null;
    // Sensors are pre-sorted by axis position. Spread c1 arrivals over [0..0.4].
    var c1 = [], c2 = [];
    for (var i = 0; i < n; i++) {
        var p = model.pts[i];
        var posMm = model.isHorizontal ? p.x : p.y;
        var fracIn = (i / (n - 1)) * 0.4;
        c1.push({ posMm: posMm, frac: fracIn });
        c2.push({ posMm: posMm, frac: fracIn + 0.5 });
    }
    var tl = { c1: c1, c2: c2, winStart: -0.1, winEnd: 1.0 };
    tl.meanSlitMm = _computeMeanSlitMm(tl);
    return tl;
}

// Mean slit width (mm) sampled across the active window. Used as the "perfect
// shutter" reference: a perfect focal-plane shutter would hold this width
// constant for the entire sweep.
function _computeMeanSlitMm(tl) {
    var SAMPLES = 64;
    // Restrict sampling to the inner window where both curtains are
    // "travelling" (between their first and last sensor crossings).
    var innerStart = Math.max(tl.c1[0].frac, tl.c2[0].frac);
    var innerEnd   = Math.min(tl.c1[tl.c1.length - 1].frac,
                              tl.c2[tl.c2.length - 1].frac);
    if (innerEnd <= innerStart) {
        // Fallback to the full window when there's no clean overlap.
        innerStart = tl.winStart;
        innerEnd = tl.winEnd;
    }
    var sum = 0, n = 0;
    for (var i = 0; i < SAMPLES; i++) {
        var f = innerStart + (i / (SAMPLES - 1)) * (innerEnd - innerStart);
        var w = Math.abs(_interpCurtainPos(tl.c1, f) - _interpCurtainPos(tl.c2, f));
        if (isFinite(w)) { sum += w; n++; }
    }
    return n > 0 ? sum / n : 0;
}

// Linear interpolation/extrapolation: given sorted-by-frac table and a target
// frac, return the corresponding mm position.
function _interpCurtainPos(table, frac) {
    if (table.length === 1) return table[0].posMm;
    if (frac <= table[0].frac) {
        // Extrapolate backward from the first segment.
        var a = table[0], b = table[1];
        var slope = (b.posMm - a.posMm) / (b.frac - a.frac);
        return a.posMm + slope * (frac - a.frac);
    }
    if (frac >= table[table.length - 1].frac) {
        // Extrapolate forward from the last segment.
        var a2 = table[table.length - 2], b2 = table[table.length - 1];
        var slope2 = (b2.posMm - a2.posMm) / (b2.frac - a2.frac);
        return b2.posMm + slope2 * (frac - b2.frac);
    }
    for (var i = 0; i < table.length - 1; i++) {
        var lo = table[i], hi = table[i + 1];
        if (frac >= lo.frac && frac <= hi.frac) {
            var span = hi.frac - lo.frac;
            if (span < 1e-9) return lo.posMm;
            return lo.posMm + (hi.posMm - lo.posMm) * (frac - lo.frac) / span;
        }
    }
    return table[table.length - 1].posMm;
}

// Compute curtain tilt offset in mm at a given travel-axis fraction.
// Returns the displacement (mm) of the bottom edge relative to the top edge
// along the travel axis. Positive = bottom edge is ahead of top edge.
// Returns 0 when per-curtain skew data is unavailable.
function _curtainTiltOffsetMm(model, tl, frac, curtainIdx) {
    // Compute the bottom-edge displacement (mm) along the travel axis relative
    // to the top edge, using per-position skew timing and curtain speed.
    // tilt_offset_mm = skew_us × curtain_speed_mm_per_us
    var leftUs, rightUs;
    if (curtainIdx === 1) {
        leftUs  = model.curtain1SkewLeftUs;
        rightUs = model.curtain1SkewRightUs;
    } else {
        leftUs  = model.curtain2SkewLeftUs;
        rightUs = model.curtain2SkewRightUs;
    }
    if (typeof leftUs !== 'number' || typeof rightUs !== 'number') return 0;

    var fracSpan = tl.winEnd - tl.winStart;
    if (fracSpan < 1e-9) return 0;
    var cTable = (curtainIdx === 1) ? tl.c1 : tl.c2;
    if (!cTable || cTable.length < 2) return 0;

    // Curtain speed from timeline slope and reference pulse duration.
    if (cTable.length >= 2) {
        var fracDiff = cTable[cTable.length - 1].frac - cTable[0].frac;
        var mmDiff = Math.abs(cTable[cTable.length - 1].posMm - cTable[0].posMm);
        if (fracDiff > 1e-9 && mmDiff > 0 && model.refDurationMs > 0) {
            var refFracSpan = model.refPulseEndFrac - model.refPulseStartFrac;
            if (refFracSpan < 1e-9) return 0;
            var usPerFrac = (model.refDurationMs * 1000) / refFracSpan;
            var speedMmPerUs = mmDiff / (fracDiff * usPerFrac);
            // Interpolate skew between left and right based on travel position.
            var travelRange = model.isHorizontal ? _EXPOSURE_GATE_W : _EXPOSURE_GATE_H;
            var currentMm = _interpCurtainPos(cTable, frac);
            var travelMin = model.isHorizontal ? -_EXPOSURE_GATE_W / 2 : -_EXPOSURE_GATE_H / 2;
            var tNorm = (travelRange > 0) ? (currentMm - travelMin) / travelRange : 0.5;
            tNorm = Math.max(0, Math.min(1, tNorm));
            var skewUs = leftUs + (rightUs - leftUs) * tNorm;
            return skewUs * speedMmPerUs;
        }
    }
    return 0;
}

// Render a single animation frame.
function _drawExposureAnimationFrame(ctx, anim, W, H) {
    var model = anim.model;
    var tl = anim.timeline;
    // Map elapsed [0..ANIM_MS] to absolute frac [winStart..winEnd].
    var t = Math.max(0, Math.min(1, anim.elapsedMs / _EXPOSURE_ANIM_MS));
    var frac = tl.winStart + t * (tl.winEnd - tl.winStart);
    var c1Mm = _interpCurtainPos(tl.c1, frac);
    var c2Mm = _interpCurtainPos(tl.c2, frac);

    // Convert to pixel coordinates along the travel axis.
    var isH = model.isHorizontal;
    var axisMin = isH ? -_EXPOSURE_GATE_W / 2 : -_EXPOSURE_GATE_H / 2;
    var axisSpan = isH ? _EXPOSURE_GATE_W : _EXPOSURE_GATE_H;
    var axisLenPx = isH ? W : H;
    // For vertical travel, canvas Y grows downward but positive y_mm is the
    // physical top of the gate, so we flip the normalized parameter. Without
    // this, a curtain entering from physical bottom (low y_mm) would visually
    // start at the top of the canvas and move downward, opposite of reality.
    function mmToPx(mm) {
        var clamped = Math.max(axisMin, Math.min(axisMin + axisSpan, mm));
        var t = (clamped - axisMin) / axisSpan;
        if (!isH) t = 1 - t;
        return t * axisLenPx;
    }
    // Unclamped variant — lets tilted parallelogram vertices extend past the
    // canvas so the browser's built-in clip handles wedge edges correctly.
    function mmToPxRaw(mm) {
        var t = (mm - axisMin) / axisSpan;
        if (!isH) t = 1 - t;
        return t * axisLenPx;
    }
    var c1Px = mmToPxRaw(c1Mm);
    var c2Px = mmToPxRaw(c2Mm);
    // Sweep direction: forward = c1 (leading) is at higher mm than c2 (trailing).
    // Translate to pixels: in forward sweep c1Px > c2Px; in reverse sweep c1Px < c2Px.
    // The slit always spans the two curtain pixel positions, regardless of direction;
    // the exposed/unexposed regions swap depending on direction.
    var loPx = Math.min(c1Px, c2Px);
    var hiPx = Math.max(c1Px, c2Px);
    // Slit region along the axis (between the two curtains).
    var slitStart = loPx, slitEnd = hiPx;

    // Background: dark (unexposed default).
    ctx.fillStyle = '#1a1a1a';
    ctx.fillRect(0, 0, W, H);

    // Helper to paint an axis-aligned region.
    function paintRegion(start, end, fill) {
        if (end <= start) return;
        ctx.fillStyle = fill;
        if (isH) ctx.fillRect(start, 0, end - start, H);
        else      ctx.fillRect(0, start, W, end - start);
    }

    // Slit (light hitting film right now).
    // Compute per-curtain tilt offsets for tilted slit visualization.
    var tilt1Mm = _curtainTiltOffsetMm(model, tl, frac, 1);
    var tilt2Mm = _curtainTiltOffsetMm(model, tl, frac, 2);
    var tilt1Px = (tilt1Mm / axisSpan) * axisLenPx;
    var tilt2Px = (tilt2Mm / axisSpan) * axisLenPx;
    // Travel-axis tilt is measured in mm; with the vertical Y flip the
    // canvas delta inverts so a positive (right-leads-left) skew becomes a
    // smaller canvas y on the right.
    if (!isH) { tilt1Px = -tilt1Px; tilt2Px = -tilt2Px; }
    var hasTilt = (Math.abs(tilt1Px) > 0.5 || Math.abs(tilt2Px) > 0.5);

    // Bounding-box visibility: the parallelogram's travel-axis extent must
    // overlap [0, axisLenPx] for any part of the slit to be on-screen.
    var slitMinPx = Math.min(c1Px, c1Px + tilt1Px, c2Px, c2Px + tilt2Px);
    var slitMaxPx = Math.max(c1Px, c1Px + tilt1Px, c2Px, c2Px + tilt2Px);
    var slitVisible = slitEnd > slitStart && slitMaxPx > 0 && slitMinPx < axisLenPx;

    if (hasTilt && slitVisible) {
        // Draw tilted slit as a parallelogram between two tilted curtain edges.
        // Each curtain edge is a line from top to bottom, offset by tiltPx.
        // For horizontal travel: curtain edge runs vertically (y=0..H),
        //   top at c1Px, bottom at c1Px + tilt1Px.
        // For vertical travel: edge runs horizontally (x=0..W),
        //   left at c1Px, right at c1Px + tilt1Px.
        ctx.fillStyle = '#f0f0f0';
        ctx.beginPath();
        if (isH) {
            ctx.moveTo(c1Px, 0);                    // curtain 1 top
            ctx.lineTo(c1Px + tilt1Px, H);          // curtain 1 bottom
            ctx.lineTo(c2Px + tilt2Px, H);          // curtain 2 bottom
            ctx.lineTo(c2Px, 0);                    // curtain 2 top
        } else {
            ctx.moveTo(0, c1Px);                    // curtain 1 left
            ctx.lineTo(W, c1Px + tilt1Px);          // curtain 1 right
            ctx.lineTo(W, c2Px + tilt2Px);          // curtain 2 right
            ctx.lineTo(0, c2Px);                    // curtain 2 left
        }
        ctx.closePath();
        ctx.fill();
    } else if (slitVisible) {
        paintRegion(slitStart, slitEnd, '#f0f0f0');
    }
    // Unexposed already covered by background.

    // Curtain edges (thin, subtle — primary visual is the slit/region fill).
    ctx.strokeStyle = 'rgba(0,0,0,0.35)';
    ctx.lineWidth = 1;
    function drawEdge(px, tiltPx) {
        ctx.beginPath();
        if (isH) { ctx.moveTo(px, 0); ctx.lineTo(px + (tiltPx || 0), H); }
        else      { ctx.moveTo(0, px); ctx.lineTo(W, px + (tiltPx || 0)); }
        ctx.stroke();
    }
    if (slitVisible) {
        drawEdge(c1Px, hasTilt ? tilt1Px : 0);
        drawEdge(c2Px, hasTilt ? tilt2Px : 0);
    }

    // "Perfect slit" reference: a line segment of length = meanSlit, drawn
    // along the travel axis and centered on the *measured* slit's midpoint.
    // It travels with the slit so you can directly compare the bright slit
    // against a constant-width ruler — slit longer than the ruler = wider
    // than mean, shorter = narrower (capping).
    var meanSlit = tl.meanSlitMm || 0;
    if (meanSlit > 0 && slitVisible) {
        var slitMidMm = (c1Mm + c2Mm) / 2;
        var halfMean = meanSlit / 2;
        var refStartMm = slitMidMm - halfMean;
        var refEndMm   = slitMidMm + halfMean;
        // Clip to gate bounds so we don't draw outside the canvas.
        var clipMin = axisMin, clipMax = axisMin + axisSpan;
        if (refEndMm > clipMin && refStartMm < clipMax) {
            var aMm = Math.max(refStartMm, clipMin);
            var bMm = Math.min(refEndMm,   clipMax);
            var aPx = mmToPx(aMm);
            var bPx = mmToPx(bMm);
            // Cross-axis position: centered on the gate's perpendicular dimension.
            var crossPx = isH ? (H / 2) : (W / 2);
            ctx.save();
            // Dark backing for contrast against the white slit.
            ctx.strokeStyle = 'rgba(0,0,0,0.6)';
            ctx.lineWidth = 5;
            if (typeof ctx.setLineDash === 'function') ctx.setLineDash([]);
            ctx.lineCap = 'round';
            ctx.beginPath();
            if (isH) { ctx.moveTo(aPx, crossPx); ctx.lineTo(bPx, crossPx); }
            else      { ctx.moveTo(crossPx, aPx); ctx.lineTo(crossPx, bPx); }
            ctx.stroke();
            // Bright cyan ruler on top.
            ctx.strokeStyle = '#00e5ff';
            ctx.lineWidth = 3;
            ctx.beginPath();
            if (isH) { ctx.moveTo(aPx, crossPx); ctx.lineTo(bPx, crossPx); }
            else      { ctx.moveTo(crossPx, aPx); ctx.lineTo(crossPx, bPx); }
            ctx.stroke();
            ctx.restore();
        }
    }

    // Sensor markers on top.
    _drawExposureSensorMarkers(ctx, model, W, H);
}

function _cancelExposureAnimation(idx) {
    var anim = _exposureAnimations[idx];
    if (!anim) return;
    if (anim.rafId) cancelAnimationFrame(anim.rafId);
    if (anim.holdTimerId) clearTimeout(anim.holdTimerId);
    delete _exposureAnimations[idx];
    // Clear the animation-driven hover guide on the measurement's waveform
    // charts. If the cancel was triggered by the user hovering one of those
    // charts, Chart.js will repaint the guide on the next mousemove.
    _pushAnimGuideToCharts(idx, null);
    var btn = document.getElementById('exposure-sim-play-' + idx);
    if (btn) { btn.innerHTML = '<span class="material-symbols-outlined">play_arrow</span>'; btn.title = 'Play curtain animation'; }
}

function startExposureSimAnimation(data, m, idx) {
    var canvas = document.getElementById('exposure-sim-' + idx);
    if (!canvas) return;
    var ctx = canvas.getContext('2d');
    var W = canvas.width, H = canvas.height;
    var model = _buildExposureSimModel(data, m);
    if (!model) return;

    var timeline = _buildExposureCurtainTimeline(model)
        || _synthesizeExposureCurtainTimeline(model);
    if (!timeline) return;

    var anim = {
        rafId: null,
        startTs: performance.now(),
        elapsedMs: 0,
        elapsedBeforePause: 0,
        state: 'playing',
        holdTimerId: null,
        model: model,
        timeline: timeline,
        data: data,
        m: m,
        idx: idx
    };
    _exposureAnimations[idx] = anim;

    var btn = document.getElementById('exposure-sim-play-' + idx);
    if (btn) { btn.innerHTML = '<span class="material-symbols-outlined">stop</span>'; btn.title = 'Stop'; }

    function step(now) {
        if (!_exposureAnimations[idx] || anim.state !== 'playing') return;
        anim.elapsedMs = anim.elapsedBeforePause + (now - anim.startTs);
        if (anim.elapsedMs >= _EXPOSURE_ANIM_MS) {
            // Final frame at t=1, then hold the static result for HOLD_MS,
            // then loop: rewind and replay until the user clicks pause.
            anim.elapsedMs = _EXPOSURE_ANIM_MS;
            _drawExposureAnimationFrame(ctx, anim, W, H);
            _pushAnimGuideToCharts(idx, _animCurrentMs(anim));
            anim.state = 'holding';
            anim.holdTimerId = setTimeout(function() {
                if (!_exposureAnimations[idx]) return;
                anim.holdTimerId = null;
                anim.elapsedBeforePause = 0;
                anim.elapsedMs = 0;
                anim.startTs = performance.now();
                anim.state = 'playing';
                anim.rafId = requestAnimationFrame(step);
            }, _EXPOSURE_HOLD_MS);
            return;
        }
        _drawExposureAnimationFrame(ctx, anim, W, H);
        _pushAnimGuideToCharts(idx, _animCurrentMs(anim));
        anim.rafId = requestAnimationFrame(step);
    }
    anim.rafId = requestAnimationFrame(step);
}

function sessionToggleExposureAnimation(idx) {
    var data = _sessionsCurrentData;
    if (!data) return;
    var ms = data.measurements || [];
    var m = ms[idx];
    if (!m) return;
    var anim = _exposureAnimations[idx];
    if (anim) {
        // Stop: cancel any pending frame/hold and restore the static overlay.
        _cancelExposureAnimation(idx);
        renderExposureSim(data, m, idx);
        return;
    }
    startExposureSimAnimation(data, m, idx);
}

// Convert a waveform x-axis value (ms relative to exposure start) to a
// timeline frac on the shared waveform window used by the curtain animation.
function _msToTimelineFrac(model, valueMs) {
    if (!model || model.refDurationMs <= 0) return null;
    var dur = model.refDurationMs;
    var pStart = model.refPulseStartFrac;
    var pSpan = model.refPulseEndFrac - pStart;
    if (pSpan <= 0) return null;
    return pStart + valueMs * pSpan / dur;
}

// Inverse of _msToTimelineFrac: given a timeline frac (winStart..winEnd),
// recover the waveform-chart ms value (relative to first sensor exposure).
function _timelineFracToMs(model, frac) {
    if (!model || model.refDurationMs <= 0) return null;
    var pStart = model.refPulseStartFrac;
    var pSpan = model.refPulseEndFrac - pStart;
    if (pSpan <= 0) return null;
    return (frac - pStart) * model.refDurationMs / pSpan;
}

// Push (or clear) the hover-guide line on every waveform chart belonging to
// measurement idx. Used by the exposure-sim animation to drive the guide
// from the curtain timeline. valueMs === null clears the guide.
function _pushAnimGuideToCharts(idx, valueMs) {
    var combinedKey = 'wf-combined-' + idx;
    var perSensorPrefix = 'wf-' + idx + '-';
    var keys = Object.keys(_sessionCharts);
    for (var i = 0; i < keys.length; i++) {
        var k = keys[i];
        if (k !== combinedKey && k.indexOf(perSensorPrefix) !== 0) continue;
        var chart = _sessionCharts[k];
        if (!chart || !chart.scales || !chart.scales.x) continue;
        if (typeof valueMs === 'number' && isFinite(valueMs)) {
            var px = chart.scales.x.getPixelForValue(valueMs);
            chart.$hoverGuideX = px;
            chart.$hoverGuideValue = valueMs;
        } else {
            chart.$hoverGuideX = null;
            chart.$hoverGuideValue = null;
        }
        try { chart.draw(); } catch (e) {}
    }
}

// Compute the current waveform ms value for an in-flight animation frame.
function _animCurrentMs(anim) {
    if (!anim || !anim.timeline || !anim.model) return null;
    var t = Math.max(0, Math.min(1, anim.elapsedMs / _EXPOSURE_ANIM_MS));
    var span = anim.timeline.winEnd - anim.timeline.winStart;
    var frac = anim.timeline.winStart + t * span;
    return _timelineFracToMs(anim.model, frac);
}

window.sessionResizeAllCharts = sessionResizeAllCharts;

// Hover-driven scrubbing: when a waveform chart reports a hovered ms value,
// draw a single animation frame at that point. Restores static view on
// mouseleave (valueMs === null). Ignored while a sweep is actively playing,
// paused, or in its post-sweep hold.
window._sessionOnWaveformHover = function(idx, valueMs) {
    var data = _sessionsCurrentData;
    if (!data) return;
    var ms = data.measurements || [];
    var m = ms[idx];
    if (!m) return;

    var canvas = document.getElementById('exposure-sim-' + idx);
    if (!canvas) return;
    var ctx = canvas.getContext('2d');
    var W = canvas.width, H = canvas.height;

    if (valueMs === null || typeof valueMs !== 'number') {
        // Mouseleave — restore static view, but only if no animation is
        // running (otherwise let the active animation keep painting).
        if (_exposureAnimations[idx]) return;
        renderExposureSim(data, m, idx);
        _pushAnimGuideToCharts(idx, null);
        return;
    }

    // Numeric hover wins over a looping animation: cancel it so the
    // scrubbed overlay frame takes over the canvas.
    if (_exposureAnimations[idx]) {
        _cancelExposureAnimation(idx);
    }

    // Sync the hover guide line across all sibling waveform charts.
    _pushAnimGuideToCharts(idx, valueMs);

    var model = _buildExposureSimModel(data, m);
    if (!model) return;
    var timeline = _buildExposureCurtainTimeline(model)
        || _synthesizeExposureCurtainTimeline(model);
    if (!timeline) return;

    var frac = _msToTimelineFrac(model, valueMs);
    if (frac === null) return;
    // Map frac onto the animation's [0..1] elapsed scale.
    var span = timeline.winEnd - timeline.winStart;
    var t = (span > 0) ? (frac - timeline.winStart) / span : 0;
    t = Math.max(0, Math.min(1, t));

    var pseudoAnim = {
        model: model,
        timeline: timeline,
        elapsedMs: t * _EXPOSURE_ANIM_MS,
        idx: idx
    };
    _drawExposureAnimationFrame(ctx, pseudoAnim, W, H);
};

// ============================================================================
// Chart.js combined waveform chart (all sensors overlaid)
// ============================================================================

function sessionCreateCombinedWaveformChart(m, idx) {
    var slot = document.getElementById('wf-combined-slot-' + idx);
    if (!slot) return;
    _sessionEnsureHoverGuidePlugin();
    slot.innerHTML = '';  // clear any previous canvas before recreating

    var sensors = m.sensors || [];
    var validSensors = [];
    for (var si = 0; si < sensors.length; si++) {
        if (sensors[si].valid && sensors[si].waveform && sensors[si].waveform.length > 0) {
            validSensors.push({ s: sensors[si], si: si });
        }
    }
    if (validSensors.length === 0) { slot.style.display = 'none'; return; }
    if (!window.Chart) {
        slot.innerHTML = '<div style="display:flex;align-items:center;justify-content:center;height:100%;color:#555;font-size:11px">Charts require internet connection for first load</div>';
        return;
    }

    // Compute global Y range in raw ADC space across all sensors
    var gYMin = Infinity, gYMax = -Infinity;
    for (var vi = 0; vi < validSensors.length; vi++) {
        var sv = validSensors[vi].s;
        var adcPad2 = (typeof sv.baseline_adc === 'number' && typeof sv.min_adc === 'number')
            ? Math.round((sv.baseline_adc - sv.min_adc) * 0.08) + 30 : 50;
        var lo = (typeof sv.min_adc      === 'number') ? sv.min_adc      - adcPad2 : 0;
        var hi = (typeof sv.baseline_adc === 'number') ? sv.baseline_adc + adcPad2 : 4096;
        if (lo < gYMin) gYMin = lo;
        if (hi > gYMax) gYMax = hi;
    }

    var isDark = document.documentElement.getAttribute('data-bs-theme') === 'dark';

    // Time conversion: derived from the earliest-opening valid sensor so the
    // chart's t=0 ms matches the first physical first-light event across all
    // sensors (shared with sessionCreateWaveformCharts and the exposure
    // animation). The stored waveform is downsampled, so sample_rate_hz does
    // NOT map to stored indices: the pulse spans
    // (pulse_end_frac - pulse_start_frac) * waveform_len samples and took
    // duration_ms milliseconds.
    var tbC = _computeSharedWaveformTimeBase(sensors);
    // Fall back to the first valid sensor only for length when no reference
    // is usable; this keeps the index-based chart rendering identical to
    // the previous behaviour.
    var refLen = (tbC ? tbC.refLen : validSensors[0].s.waveform.length);
    var msPerSample = tbC ? tbC.msPerSample : 0;
    var exposureStartIdx = tbC ? tbC.exposureStartIdx : 0;
    var hasTimeAxis = !!tbC;

    function idxToMs(i) { return (i - exposureStartIdx) * msPerSample; }

    var annotations = {};

    // One dataset per sensor: raw ADC values, fill to baseline
    var datasets = [];
    for (var vi = 0; vi < validSensors.length; vi++) {
        var sv   = validSensors[vi].s;
        var si   = validSensors[vi].si;
        var pal  = _WF_SENSOR_PALETTE[si % _WF_SENSOR_PALETTE.length];
        var wf   = sv.waveform;
        var base    = sv.baseline_adc;
        var devVals = [];
        for (var pi = 0; pi < wf.length; pi++) {
            devVals.push({ x: hasTimeAxis ? idxToMs(pi) : pi, y: wf[pi] });
        }
        datasets.push({
            label: 'S' + (si + 1),
            data: devVals,
            borderColor: pal.faint,
            borderWidth: 1.5,
            pointRadius: 0,
            fill: (typeof sv.baseline_adc === 'number') ? { value: sv.baseline_adc } : false,
            backgroundColor: pal.fill,
            tension: 0
        });
    }

    var canvas = document.createElement('canvas');
    canvas.style.cssText = 'display:block;width:100%;height:160px';
    slot.innerHTML = '';
    slot.appendChild(canvas);

    var xMin = hasTimeAxis ? tbC.xMin : 0;
    var xMax = hasTimeAxis ? tbC.xMax : refLen - 1;

    var xAxisCfg = {
        type: 'linear',
        min: xMin,
        max: xMax
    };
    if (hasTimeAxis) {
        xAxisCfg.display = true;
        xAxisCfg.border = { display: false };
        xAxisCfg.ticks = { font: { size: 9 }, maxTicksLimit: 8, padding: 2, callback: function(v) { return v.toFixed(1) + ' ms'; } };
        xAxisCfg.grid  = { color: isDark ? 'rgba(255,255,255,0.08)' : 'rgba(0,0,0,0.06)', tickLength: 0 };
    } else {
        xAxisCfg.display = false;
    }

    var chartKey = 'wf-combined-' + idx;
    if (_sessionCharts[chartKey]) { _sessionCharts[chartKey].destroy(); }
    _sessionCharts[chartKey] = new Chart(canvas, {
        type: 'line',
        data: { datasets: datasets },
        options: {
            animation: false,
            responsive: true,
            maintainAspectRatio: false,
            plugins: {
                legend:     { display: false },
                tooltip:    { enabled: false },
                annotation: { annotations: annotations },
                sessionHoverGuide: {
                    enabled: true,
                    measurementIdx: idx,
                    unit: hasTimeAxis ? 'ms' : '',
                    decimals: hasTimeAxis ? 2 : 1,
                    lineColor: isDark ? 'rgba(255,255,255,0.45)' : 'rgba(0,0,0,0.35)',
                    labelBackgroundColor: isDark ? 'rgba(25,25,25,0.86)' : 'rgba(255,255,255,0.92)',
                    labelColor: isDark ? '#f5f5f5' : '#222'
                }
            },
            scales: {
                x: xAxisCfg,
                y: { type: 'linear', display: false, min: gYMin, max: gYMax }
            }
        }
    });
}

// ============================================================================
// Chart.js deviation scatter strip (summary chart in session header)
// ============================================================================

function sessionCreateDeviationChart(containerId, ms) {
    var container = document.getElementById(containerId);
    if (!container || !ms || ms.length === 0) return;
    if (!window.Chart) {
        container.innerHTML = '<div class="text-muted small py-2">Charts require internet connection for first load</div>';
        return;
    }

    // Collect unique speeds ordered slowest first (longest duration at top)
    var speedOrder = [], speedMap = {};
    for (var i = 0; i < ms.length; i++) {
        var spd = ms[i].nearest_speed || '—';
        if (!speedMap[spd]) {
            speedMap[spd] = { dur: ms[i].nearest_duration_ms || 0, pts: [] };
            speedOrder.push(spd);
        }
        speedMap[spd].pts.push({ dev: ms[i].deviation_pct, verdict: ms[i].verdict || 'pass', measIdx: i });
    }
    speedOrder.sort(function(a, b) { return (speedMap[b].dur || 0) - (speedMap[a].dur || 0); });

    var nSpeeds = speedOrder.length;
    // Scale height with data: 60px per row, min 140px, max 280px
    var height = Math.max(140, Math.min(280, nSpeeds * 60));

    // Build scatter point arrays by colour band, plus per-group average markers.
    // X values are in stops: stops = log2(1 + dev%/100). Bands are named for the
    // background regions they fall in, not for verdicts (a single shot landing in
    // the red band does not fail the speed group on its own — the multi-metric
    // verdict in the summary table is authoritative).
    var greenBandData = [], amberBandData = [], redBandData = [], avgData = [];
    // Dot color thresholds match the chart bands (±⅓ green, ±½ yellow, beyond red).
    var devWarnStops = 1/3;
    var devFailStops = 1/2;
    if (_verdictThresholds && _verdictThresholds.deviation) {
        devWarnStops = _verdictThresholds.deviation.warning;
        devFailStops = _verdictThresholds.deviation.fail;
    }
    for (var yi = 0; yi < speedOrder.length; yi++) {
        var pts = speedMap[speedOrder[yi]].pts;
        var sum = 0;
        for (var pi = 0; pi < pts.length; pi++) {
            sum += pts[pi].dev;
            var stops = Math.log2(1 + pts[pi].dev / 100);
            var absStops = Math.abs(stops);
            var pt = { x: stops, y: yi, measIdx: pts[pi].measIdx };
            if      (absStops > devFailStops) redBandData.push(pt);
            else if (absStops > devWarnStops) amberBandData.push(pt);
            else                              greenBandData.push(pt);
        }
        if (pts.length >= 2) {
            avgData.push({ x: Math.log2(1 + (sum / pts.length) / 100), y: yi, measIdx: pts[0].measIdx });
        }
    }

    var isDark = document.documentElement.getAttribute('data-bs-theme') === 'dark';
    var gridColor    = isDark ? 'rgba(255,255,255,0.15)' : 'rgba(0,0,0,0.18)';
    var tickColorX   = isDark ? '#bbb' : '#444';
    var tickColorY   = isDark ? '#ccc' : '#333';
    var zeroColor    = isDark ? 'rgba(255,255,255,0.65)' : 'rgba(0,0,0,0.55)';
    var avgColor     = isDark ? 'rgba(255,255,255,0.90)' : 'rgba(20,20,20,0.90)';
    var avgLineColor = isDark ? 'rgba(255,255,255,0.50)' : 'rgba(20,20,20,0.40)';

    var annotations = {
        zeroLine: {
            type: 'line', scaleID: 'x', value: 0,
            borderColor: zeroColor, borderWidth: 2
        },
        yellowBandL: {
            type: 'box',
            xMin: -devFailStops, xMax: -devWarnStops,
            backgroundColor: isDark ? 'rgba(221,170,68,0.18)' : 'rgba(200,140,0,0.15)',
            borderWidth: 0
        },
        yellowBandR: {
            type: 'box',
            xMin: devWarnStops, xMax: devFailStops,
            backgroundColor: isDark ? 'rgba(221,170,68,0.18)' : 'rgba(200,140,0,0.15)',
            borderWidth: 0
        },
        greenBand: {
            type: 'box',
            xMin: -devWarnStops, xMax: devWarnStops,
            backgroundColor: isDark ? 'rgba(68,200,68,0.13)' : 'rgba(40,160,40,0.18)',
            borderWidth: 0
        }
    };

    container.style.height = height + 'px';
    container.innerHTML = '';
    var canvas = document.createElement('canvas');
    canvas.style.cursor = 'pointer';
    container.appendChild(canvas);

    // HTML legend using inline SVG icons — renders correctly in print
    // regardless of browser "print background graphics" setting.
    var legendEl = document.createElement('div');
    legendEl.style.cssText = 'display:flex;justify-content:center;gap:16px;padding-top:6px;font-size:12px;color:' + tickColorX;
    var legendItems = [
        { color: 'rgba(68,180,68,0.85)',  shape: 'circle',  label: 'green band' },
        { color: 'rgba(221,170,68,0.85)', shape: 'circle',  label: 'amber band' },
        { color: 'rgba(200,68,68,0.85)',  shape: 'circle',  label: 'red band' },
        { color: avgColor,                shape: 'diamond', label: 'group average' }
    ];
    for (var li = 0; li < legendItems.length; li++) {
        var item = legendItems[li];
        var span = document.createElement('span');
        span.style.cssText = 'display:inline-flex;align-items:center;gap:5px';
        var svgNS = 'http://www.w3.org/2000/svg';
        var svg = document.createElementNS(svgNS, 'svg');
        svg.setAttribute('width', '10');
        svg.setAttribute('height', '10');
        svg.setAttribute('viewBox', '0 0 10 10');
        svg.style.flexShrink = '0';
        var shape;
        if (item.shape === 'diamond') {
            shape = document.createElementNS(svgNS, 'polygon');
            shape.setAttribute('points', '5,0 10,5 5,10 0,5');
        } else {
            shape = document.createElementNS(svgNS, 'circle');
            shape.setAttribute('cx', '5');
            shape.setAttribute('cy', '5');
            shape.setAttribute('r', '4.5');
        }
        shape.setAttribute('fill', item.color);
        svg.appendChild(shape);
        var txt = document.createTextNode(item.label);
        span.appendChild(svg);
        span.appendChild(txt);
        legendEl.appendChild(span);
    }
    var legendContainer = document.getElementById('session-deviation-legend');
    if (legendContainer) {
        legendContainer.innerHTML = '';
        legendContainer.appendChild(legendEl);
    }

    // Capture speedOrder in closure for tooltip and Y-axis callbacks
    var capturedSpeedOrder = speedOrder;

    var chartKey = 'deviation';
    if (_sessionCharts[chartKey]) { _sessionCharts[chartKey].destroy(); }
    _sessionCharts[chartKey] = new Chart(canvas, {
        type: 'scatter',
        data: {
            datasets: [
                {
                    label: 'green band',
                    data: greenBandData,
                    backgroundColor: 'rgba(68,180,68,0.85)',
                    pointRadius: 3, pointHoverRadius: 5
                },
                {
                    label: 'amber band',
                    data: amberBandData,
                    backgroundColor: 'rgba(221,170,68,0.85)',
                    pointRadius: 3, pointHoverRadius: 5
                },
                {
                    label: 'red band',
                    data: redBandData,
                    backgroundColor: 'rgba(200,68,68,0.85)',
                    pointRadius: 3, pointHoverRadius: 5
                },
                {
                    // Group average marker — diamond + connecting line, shown when ≥2 measurements
                    label: 'group average',
                    data: avgData,
                    backgroundColor: avgColor,
                    pointRadius: 8, pointStyle: 'rectRot', pointHoverRadius: 10,
                    showLine: true,
                    borderColor: avgLineColor,
                    borderWidth: 1.5,
                    borderDash: [3, 3]
                }
            ]
        },
        options: {
            animation: false,
            responsive: true,
            maintainAspectRatio: false,
            onClick: function(evt, activeEls, chart) {
                if (!activeEls || activeEls.length === 0) return;
                var el   = activeEls[0];
                var pt   = chart.data.datasets[el.datasetIndex].data[el.index];
                if (typeof pt.measIdx !== 'number') return;
                var card = document.getElementById('meas-card-' + pt.measIdx);
                if (card) card.scrollIntoView({ behavior: 'smooth', block: 'start' });
            },
            plugins: {
                legend: { display: false },
                tooltip: {
                    callbacks: {
                        label: function(item) {
                            var si   = Math.round(item.parsed.y);
                            var spd  = (si >= 0 && si < capturedSpeedOrder.length) ? capturedSpeedOrder[si] : '\u2014';
                            var suffix = item.dataset.label === 'avg' ? ' (avg)' : '';
                            return spd + ': ' + _stopsLabel(item.parsed.x) + ' stops' + suffix;
                        }
                    }
                },
                annotation: { annotations: annotations }
            },
            scales: {
                x: {
                    type: 'linear',
                    min: -2, max: 2,
                    afterBuildTicks: function(scale) {
                        scale.ticks = [-2, -1, -2/3, -1/2, -1/3, 0, 1/3, 1/2, 2/3, 1, 2].map(function(v) { return { value: v }; });
                    },
                    ticks: {
                        color: tickColorX,
                        callback: function(v) { return _stopsLabel(v); }
                    },
                    grid: { color: gridColor }
                },
                y: {
                    type: 'linear',
                    min: -0.5,
                    max: nSpeeds - 0.5,
                    // Force ticks to exact integer positions so grid lines
                    // always align with each speed row's center.
                    afterBuildTicks: function(scale) {
                        scale.ticks = [];
                        for (var ti = 0; ti < nSpeeds; ti++) {
                            scale.ticks.push({ value: ti });
                        }
                    },
                    ticks: {
                        color: tickColorY,
                        callback: function(val) {
                            var i = Math.round(val);
                            return (i >= 0 && i < capturedSpeedOrder.length) ? _formatSpeedLabel(capturedSpeedOrder[i]) : '';
                        }
                    },
                    grid: { color: gridColor }
                }
            }
        }
    });
}

// ============================================================================
// Session summary table
// ============================================================================

function _parseDenominator(label) {
    var parts = String(label).split('/');
    return parts.length >= 2 ? (parseInt(parts[1]) || 0) : 0;
}

function sessionBuildSummaryTable(ms) {
    var container = document.getElementById('summary-table-container');
    if (!container) return;
    if (!ms || ms.length === 0) { container.style.display = 'none'; return; }

    // Group measurements by speed. ms is already sorted by nominal duration, so
    // group.indices[k] == the meas-card-N index of each measurement in rendered order.
    var grouped = _groupBySpeed(ms);
    var groupOrder = grouped.order.slice();
    var groups = grouped.groups;

    // Sort groups fastest first (largest denominator first)
    groupOrder.sort(function(a, b) { return _parseDenominator(b) - _parseDenominator(a); });

    // Per-group statistics
    var rows = [];
    var totalCount = 0;
    var trendPoints = [];
    for (var gi = 0; gi < groupOrder.length; gi++) {
        var g = groups[groupOrder[gi]];
        var n = g.measurements.length;
        totalCount += n;

        // Collect durations + dev_stops from measurements (no longer pre-extracted by helper).
        var durs = [], devStops = [];
        for (var k = 0; k < n; k++) {
            durs.push(g.measurements[k].avg_duration_ms);
            devStops.push(g.measurements[k].deviation_stops);
        }

        var sumD = 0, minD = durs[0], maxD = durs[0];
        for (var k = 0; k < n; k++) {
            sumD += durs[k];
            if (durs[k] < minD) minD = durs[k];
            if (durs[k] > maxD) maxD = durs[k];
        }
        var meanD = sumD / n;

        var sumDev = 0;
        for (var k = 0; k < n; k++) sumDev += devStops[k];
        var avgDev = sumDev / n;

        var spreadVal = null; // numeric spread for verdict
        var spreadStr = '\u2014';
        if (n >= 2 && meanD > 0) {
            spreadVal = (maxD - minD) / meanD * 100;
            spreadStr = spreadVal.toFixed(1) + '%';
        }

        var grpMinR = null, grpMaxR = null;
        // Per-shot within-gate Δ% (max-min ratio range across sensors), then
        // averaged across shots at this speed. Captures "how unbalanced was
        // the slit during a typical shot at this speed" rather than the
        // (noisier) min/max envelope across all shots.
        var deltaPctSum = 0, deltaPctN = 0;
        // Mean opening / closing ms across all valid sensors and all shots.
        var c1Sum = 0, c1N = 0, c2Sum = 0, c2N = 0;
        for (var k = 0; k < n; k++) {
            var meas = g.measurements[k];
            var cr = _mCurtainRatioRange(meas);
            if (cr) {
                if (grpMinR === null || cr.min < grpMinR) grpMinR = cr.min;
                if (grpMaxR === null || cr.max > grpMaxR) grpMaxR = cr.max;
                deltaPctSum += (cr.max - cr.min) * 100;
                deltaPctN++;
            }
            var sensors = meas.sensors || [];
            for (var si = 0; si < sensors.length; si++) {
                var ed = sensors[si]._edges;
                if (!ed) continue;
                // Skip sensors whose curtain stats were flagged invalid by the
                // device (full-open mode or sensor recovery tail).
                if (ed.curtainRatio === null) continue;
                if (typeof ed.curtain1Ms === 'number') { c1Sum += ed.curtain1Ms; c1N++; }
                if (typeof ed.curtain2Ms === 'number') { c2Sum += ed.curtain2Ms; c2N++; }
            }
        }
        var openingMs   = (c1N > 0) ? c1Sum / c1N : null;
        var closingMs   = (c2N > 0) ? c2Sum / c2N : null;
        var balanceVal  = (openingMs && openingMs > 0 && closingMs !== null) ? closingMs / openingMs : null;
        var balanceDelta = (deltaPctN > 0) ? deltaPctSum / deltaPctN : null;
        var openingStr = (openingMs !== null) ? openingMs.toFixed(2) : '\u2014';
        var closingStr = (closingMs !== null) ? closingMs.toFixed(2) : '\u2014';
        var balanceStr = '\u2014';
        if (balanceVal !== null) {
            balanceStr = balanceVal.toFixed(2);
            if (balanceDelta !== null && balanceDelta >= 0.5) {
                balanceStr += ' <span class="text-muted">(\u0394' + Math.round(balanceDelta) + '%)</span>';
            }
        }
        var curtainRatioStr = (grpMinR !== null) ? _curtainRatioRangeStr({ min: grpMinR, max: grpMaxR }) : '\u2014';

        var repeatVal = null; // numeric CV% for verdict
        var repeatStr = '\u2014';
        if (n >= 3 && meanD > 0) {
            var variance = 0;
            for (var k = 0; k < n; k++) {
                var diff = durs[k] - meanD;
                variance += diff * diff;
            }
            repeatVal = Math.sqrt(variance / n) / meanD * 100;
            repeatStr = '\u00b1' + repeatVal.toFixed(1) + '%';
        }

        // Full-frame capping estimate from measurement array.
        var frameCappingVal = _frameCappingFromMeasurements(g.measurements);
        var frameCappingStr = '\u2014';
        if (frameCappingVal !== null) {
            frameCappingStr = frameCappingVal >= 0.01 ? frameCappingVal.toFixed(2) : '<0.01';
        }

        var verdict = _rowVerdict(avgDev);

        var avgDevStr = _signPrefix(avgDev) + avgDev.toFixed(2);

        var targetStr = g.nominal_ms > 0 ? g.nominal_ms.toFixed(2) : '\u2014';

        if (g.nominal_ms > 0) trendPoints.push({ x: Math.log2(g.nominal_ms), y: avgDev });

        rows.push({ label: g.label, verdict: verdict, avgDevStr: avgDevStr,
                    avgDev: avgDev, spreadVal: spreadVal, frameCappingVal: frameCappingVal,
                    repeatVal: repeatVal, nominalMs: g.nominal_ms,
                    spreadStr: spreadStr, frameCappingStr: frameCappingStr,
                    repeatStr: repeatStr, targetStr: targetStr,
                    curtainRatioStr: curtainRatioStr,
                    openingStr: openingStr, closingStr: closingStr, balanceStr: balanceStr,
                    balanceVal: balanceVal, balanceDelta: balanceDelta,
                    n: n,
                    durations: durs, cardIndices: g.indices });
    }

    // Speed trend: linear regression X=log2(nominal_ms), Y=avg deviation_stops, display=-slope
    var trendStr = '\u2014 per doubling';
    if (trendPoints.length >= 3) {
        var slope = _linearRegressionSlope(trendPoints);
        if (slope !== null) {
            var trendVal = -slope;
            trendStr = _signPrefix(trendVal) + trendVal.toFixed(2) + ' stops per doubling';
        }
    }

    // Column header tooltip text
    var tt = {
        speed:   'The shutter speed at which this test was run (e.g., 1/500, 1/250). This is the nominal target speed the camera\'s shutter is supposed to achieve.',
        verdict: 'Verdict for this speed based on absolute deviation in stops. Pass: within ±1/3 stop. Warning: between 1/3 and 1/2 stop. Fail: more than 1/2 stop off nominal. Secondary metrics (capping, spread, repeatability) are shown for diagnostic context but do not affect the verdict.',
        avgDev:  'The average error from the target speed, measured in stops. Negative means the shutter is running faster than nominal; positive means slower.',
        spread:  'Shot spread — shot-to-shot variation in measured exposure at this speed, as a percentage. Tighter is better. Under 5% is excellent; above 10% suggests wear or lubrication issues. (Distinct from sensor spread, which is the within-shot variation across sensors.)',
        frameCapping: 'Estimated total capping across the full 35mm frame (' + _FILM_DIAGONAL_MM + ' mm diagonal), in stops. < \u2153 stop: within spec. \u2153\u2013\u2154 stop: visible in slides, adjust during CLA. > \u2154 stop: visible in prints, needs service.',
        repeat:  'Standard deviation of measurements as a coefficient of variation (CV%). Under 5% indicates excellent repeatability; 5\u201310% is acceptable; above 10% suggests wear or mechanical play. Only shown if 3+ measurements were taken.',
        curtain: 'Ratio of 2nd curtain edge time to 1st curtain edge time (2nd \u00f7 1st), across all sensors and measurements at this speed. A ratio near 1.00 means both curtains take equal time to cross the gate, which is ideal. A range (X.XX \u2192 Y.YY) spans the observed variation.',
        curtainGroup: 'When each curtain edge crosses the sensor positions, summarised across all sensors and shots at this speed, plus the closing\u00f7opening balance. 1.00 = perfectly balanced (closing curtain takes the same time as opening). The optional (\u0394N%) suffix on Balance shows the average within-gate ratio spread per shot \u2014 large values indicate the slit width changes during sweep.',
        opening: 'Average time the 1st (opening) curtain edge takes to cross the gate at this speed, across all valid sensors and shots. The value should be roughly stable shot-to-shot at a given speed.',
        closing: 'Average time the 2nd (closing) curtain edge takes to cross the gate at this speed, across all valid sensors and shots. Compare against Opening: a consistent mismatch points at curtain spring imbalance.',
        balance: 'Closing \u00f7 Opening curtain edge time at this speed. 1.00 = balanced. Values consistently away from 1.00 across multiple speeds suggest curtain spring imbalance \u2014 a common CLA target on focal-plane shutters. (\u0394N%) suffix shows average within-shot ratio spread when \u2265 0.5%.',
        indiv:   'Raw measured shutter duration in milliseconds for each test, in capture order. Use these to spot patterns like improving or degrading performance across a test sequence.',
        trend:   'Drift pattern across the speed range, in stops per doubling. Negative = shutter slows at fast speeds (typical aging); positive = speeds up. Under \u00b10.1 stops is healthy; under \u00b10.3 is acceptable; above \u00b10.3 needs service. Only shown if 3+ speeds tested.'
    };

    var html = '<div class="card-body">';
    html += '<h6 class="mb-3">Speed summary</h6>';
    html += '<div class="table-responsive">';
    html += '<table class="table table-sm sst-table mb-0">';
    html += '<thead>';
    // Two-row header: top row has a colspan banner over the curtain group.
    html += '<tr>';
    var unitPad = '<br><span class="sst-unit-spacer">(x)</span>';
    html += '<th rowspan="2"><span title="' + escapeHtml(tt.verdict) + '">Verdict' + unitPad + '</span></th>';
    html += '<th rowspan="2"><span title="' + escapeHtml(tt.speed)   + '">Speed<br><span class="sst-unit">(s)</span></span></th>';
    html += '<th rowspan="2"><span title="' + escapeHtml(tt.avgDev)  + '">Avg deviation<br><span class="sst-unit">(stops)</span></span></th>';
    html += '<th rowspan="2"><span title="' + escapeHtml(tt.spread)  + '">Shot spread' + unitPad + '</span></th>';
    html += '<th rowspan="2"><span title="' + escapeHtml(tt.frameCapping) + '">Frame capping<br><span class="sst-unit">(stops)</span></span></th>';
    html += '<th rowspan="2"><span title="' + escapeHtml(tt.repeat)  + '">Repeatability' + unitPad + '</span></th>';
    html += '<th colspan="3" class="text-center sst-curtain-group"><span title="' + escapeHtml(tt.curtainGroup) + '">Curtain edge timing</span></th>';
    html += '<th rowspan="2" class="sst-indiv"><span title="' + escapeHtml(tt.indiv) + '">Individual measurements<br><span class="sst-unit">(ms)</span></span></th>';
    html += '</tr><tr>';
    html += '<th class="sst-curtain-sub sst-curtain-edge-left"><span title="' + escapeHtml(tt.opening) + '">1st curtain<br><span class="sst-unit">(ms)</span></span></th>';
    html += '<th class="sst-curtain-sub"><span title="' + escapeHtml(tt.closing) + '">2nd curtain<br><span class="sst-unit">(ms)</span></span></th>';
    html += '<th class="sst-curtain-sub sst-curtain-edge-right"><span title="' + escapeHtml(tt.balance) + '">Balance' + unitPad + '</span></th>';
    html += '</tr></thead>';
    html += '<tbody>';
    for (var ri = 0; ri < rows.length; ri++) {
        var r = rows[ri];
        var firstCard = r.cardIndices[0];
        var devCls    = _metricColor(r.avgDev, 'deviation');
        var spreadCls = _metricColor(r.spreadVal, 'spread');
        var capCls    = _metricColor(r.frameCappingVal, 'frame_capping');
        var repCls    = _metricColor(r.repeatVal, 'repeatability');
        html += '<tr style="cursor:pointer" onclick="var c=document.getElementById(\'meas-card-' + firstCard + '\');if(c)c.scrollIntoView({behavior:\'smooth\',block:\'start\'});">';
        html += '<td>'                      + sessionVerdictBadge(r.verdict) + '</td>';
        html += '<td class="fw-semibold">' + escapeHtml(_formatSpeedLabel(r.label)) + '</td>';
        html += '<td class="' + devCls + '">' + escapeHtml(r.avgDevStr) + ' <span class="text-muted">(n=' + r.n + ')</span></td>';
        html += '<td class="' + spreadCls + '">'  + escapeHtml(r.spreadStr) + '</td>';
        html += '<td class="' + capCls + '">'     + escapeHtml(r.frameCappingStr) + '</td>';
        html += '<td class="' + repCls + '">'     + escapeHtml(r.repeatStr) + '</td>';
        html += '<td class="sst-curtain-sub sst-curtain-edge-left">' + escapeHtml(r.openingStr) + '</td>';
        html += '<td class="sst-curtain-sub">' + escapeHtml(r.closingStr) + '</td>';
        html += '<td class="sst-curtain-sub sst-curtain-edge-right">' + r.balanceStr + '</td>';
        var indivHtml = '';
        for (var k = 0; k < r.cardIndices.length; k++) {
            if (k > 0) indivHtml += ', ';
            var dv = r.durations[k];
            var dvStr = typeof dv === 'number' ? dv.toFixed(2) : '\u2014';
            indivHtml += '<a href="#" onclick="event.stopPropagation();var c=document.getElementById(\'meas-card-' + r.cardIndices[k] + '\');if(c)c.scrollIntoView({behavior:\'smooth\',block:\'start\'});return false;">' + escapeHtml(dvStr) + '</a>';
        }
        indivHtml += ' <span class="text-muted">(target: ' + escapeHtml(r.targetStr) + ')</span>';
        html += '<td class="sst-indiv" onclick="event.stopPropagation()">' + indivHtml + '</td>';
        html += '</tr>';
    }
    html += '</tbody>';
    var trendTip = escapeHtml(tt.trend);
    html += '<tfoot><tr class="sst-totals">';
    html += '<td colspan="9" class="small text-muted">Total measurements: ' + totalCount
         + ' &nbsp;&middot;&nbsp; <span title="' + trendTip + '">Speed trend: ' + escapeHtml(trendStr) + '</span></td>';
    html += '<td class="sst-indiv"></td>';
    html += '</tr></tfoot>';
    html += '</table></div></div>';
    container.innerHTML = html;
    container.style.display = '';
}

// ============================================================================
// Helpers
// ============================================================================

// Format a stops value as a signed fraction string: +1/3, -1/2, +1, 0, etc.
var _STOPS_FRACS = [[1,3],[1,2],[2,3],[1,1],[4,3],[3,2],[2,1],[3,1]];
function _stopsLabel(v) {
    if (Math.abs(v) < 0.01) return '0';
    var sign = v > 0 ? '+' : '\u2212';
    var abs  = Math.abs(v);
    for (var i = 0; i < _STOPS_FRACS.length; i++) {
        if (Math.abs(abs - _STOPS_FRACS[i][0] / _STOPS_FRACS[i][1]) < 0.02) {
            return sign + (_STOPS_FRACS[i][1] === 1 ? _STOPS_FRACS[i][0] : _STOPS_FRACS[i][0] + '/' + _STOPS_FRACS[i][1]);
        }
    }
    return (v > 0 ? '+' : '') + v.toFixed(2);
}

function sessionVerdictBadge(verdict) {
    var cls = verdict === 'pass' ? 'bg-success' : verdict === 'warning' ? 'bg-warning text-dark' : 'bg-danger';
    return '<span class="badge ' + cls + '">' + escapeHtml(_verdictLabel(verdict)) + '</span>';
}

// Title-Case verdict for display: 'pass' → 'Pass', 'warning' → 'Warning', 'fail' → 'Fail'.
// Canonical lowercase keys are kept everywhere internally; only badges, banners,
// and the AI prompt format with this helper.
function _verdictLabel(v) {
    return v === 'pass' ? 'Pass' : v === 'warning' ? 'Warning' : v === 'fail' ? 'Fail' : (v || '');
}

// Display-format a speed label. Whole-second speeds keep their unit (4s, 2s, 1s)
// because a bare "4" is ambiguous (f-stop? clicks?). Fractional speeds drop the
// trailing 's' (1/500s → 1/500) since the slash already signals a duration.
function _formatSpeedLabel(label) {
    if (typeof label !== 'string') return label;
    return /^1\/[0-9]+s$/.test(label) ? label.replace(/s$/, '') : label;
}

function sessionFormatMetaValue(label, value) {
    if (label === 'ADC Sample Rate' && typeof value === 'number')
        return (value / 1000).toFixed(1) + ' kHz';
    if (label === 'Ambient Baseline' && typeof value === 'number')
        return value + ' ADC';
    return String(value);
}

function escapeHtml(s) {
    if (s == null) return '';
    return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}

// ============================================================================
// AI Diagnostic Prompt — math helpers
// ============================================================================

function _promptMean(arr) {
    if (!arr || arr.length === 0) return 0;
    var sum = 0;
    for (var i = 0; i < arr.length; i++) sum += arr[i];
    return sum / arr.length;
}

function _promptStdDev(arr, mean) {
    if (!arr || arr.length < 2) return 0;
    var sum = 0;
    for (var i = 0; i < arr.length; i++) { var d = arr[i] - mean; sum += d * d; }
    return Math.sqrt(sum / arr.length);
}

// verdict → markdown status icon
function _promptVerdictIcon(verdict) {
    if (verdict === 'pass')    return '\u2713';      // ✓
    if (verdict === 'warning') return '\u26a0\ufe0f'; // ⚠️
    return '\u2717';                                   // ✗
}

// Escape pipe chars that would break markdown table cells in speed labels
function _promptEscapeMd(s) {
    return String(s == null ? '' : s).replace(/\|/g, '\\|');
}

// ============================================================================
// AI Diagnostic Prompt — markdown generator
// ============================================================================

function generateDiagnosticPrompt(sessionData) {
    var ms = (sessionData.measurements || []).slice();
    if (ms.length === 0) return null;

    // Sort fast to slow (ascending nominal duration) — matches summary table order
    ms.sort(function(a, b) { return (a.nearest_duration_ms || 0) - (b.nearest_duration_ms || 0); });
    _annotateMeasurementEdges(ms);  // idempotent: ensures s._edges is set when called outside detail render

    var now = new Date().toISOString();
    var totalMeasurements = ms.length;

    // --- Group measurements by speed (fastest first by denominator) ---
    var grouped = _groupBySpeed(ms);
    var groups = grouped.groups;
    var groupOrder = grouped.order.slice();
    groupOrder.sort(function(a, b) { return _parseDenominator(b) - _parseDenominator(a); });

    // --- Per-speed statistics + waveform metrics ---
    var speedStats = [];
    var trendPoints = [];
    var allVerdicts = [];

    for (var gi = 0; gi < groupOrder.length; gi++) {
        var g = groups[groupOrder[gi]];
        var gms = g.measurements;
        var n = gms.length;

        var durations    = gms.map(function(m) { return m.avg_duration_ms; });
        var deviationPct = gms.map(function(m) { return m.deviation_pct; });
        var deviationStp = gms.map(function(m) { return m.deviation_stops; });

        var avgDur      = _promptMean(durations);
        var avgDevPct   = _promptMean(deviationPct);
        var avgDevStops = _promptMean(deviationStp);
        var cv          = (n >= 3 && avgDur > 0) ? (_promptStdDev(durations, avgDur) / avgDur) * 100 : null;
        var spread      = (n >= 2 && avgDur > 0) ? ((Math.max.apply(null, durations) - Math.min.apply(null, durations)) / avgDur) * 100 : null;

        // Verdict (deviation-only) via shared helper.
        var groupVerdict = _computeGroupVerdict(gms);
        for (var vi = 0; vi < gms.length; vi++) {
            allVerdicts.push(gms[vi].verdict || 'pass');
        }

        // Waveform metrics: read memoized s._edges (set by _annotateMeasurementEdges).
        var curtain1Times = [], curtain2Times = [], curtainRatios = [], dwellTimes = [];
        var ratioMin = null, ratioMax = null;  // across all sensors of all shots in this speed
        for (var mi = 0; mi < gms.length; mi++) {
            var sensors = gms[mi].sensors || [];
            for (var si = 0; si < sensors.length; si++) {
                var edge = sensors[si]._edges;
                if (!edge) continue;
                // Skip sensors whose curtain stats were flagged invalid by the
                // device (full-open mode or sensor recovery tail) \u2014 c1/c2/dwell
                // are not curtain transit times in that regime.
                if (edge.curtainRatio === null) continue;
                curtain1Times.push(edge.curtain1Ms);
                curtain2Times.push(edge.curtain2Ms);
                dwellTimes.push(edge.dwellMs);
                curtainRatios.push(edge.curtainRatio);
                if (ratioMin === null || edge.curtainRatio < ratioMin) ratioMin = edge.curtainRatio;
                if (ratioMax === null || edge.curtainRatio > ratioMax) ratioMax = edge.curtainRatio;
            }
        }

        // Per-shot within-shot sensor spread (the secondary verdict metric):
        // average of m.spread_pct across shots that have it.
        var sensorSpreadSum = 0, sensorSpreadN = 0;
        for (var msi = 0; msi < gms.length; msi++) {
            var sp = gms[msi].spread_pct;
            if (typeof sp === 'number' && gms[msi].sensor_count >= 2) {
                sensorSpreadSum += sp;
                sensorSpreadN++;
            }
        }
        var avgSensorSpread = sensorSpreadN > 0 ? sensorSpreadSum / sensorSpreadN : null;

        // Frame capping (mirrors summary table computation).
        var frameCapping = _frameCappingFromMeasurements(gms);

        if (g.nominal_ms > 0) {
            trendPoints.push({ x: Math.log2(g.nominal_ms), y: avgDevStops });
        }

        speedStats.push({
            label:        g.label,
            count:        n,
            nominalMs:    g.nominal_ms,
            avgDur:       avgDur,
            avgDevPct:    avgDevPct,
            minDevPct:    Math.min.apply(null, deviationPct),
            maxDevPct:    Math.max.apply(null, deviationPct),
            avgDevStops:  avgDevStops,
            cv:           cv,
            spread:       spread,
            sensorSpread: avgSensorSpread,
            frameCapping: frameCapping,
            verdict:      groupVerdict,
            durations:    durations,
            avgCurtain1:  curtain1Times.length > 0 ? _promptMean(curtain1Times) : null,
            avgCurtain2:  curtain2Times.length > 0 ? _promptMean(curtain2Times) : null,
            avgDwell:     dwellTimes.length    > 0 ? _promptMean(dwellTimes)    : null,
            avgCurtainRatio: curtainRatios.length > 0 ? _promptMean(curtainRatios) : null,
            ratioMin:     ratioMin,
            ratioMax:     ratioMax
        });
    }

    // --- Session-level analysis ---
    var verdictRank2 = { 'pass': 0, 'warning': 1, 'fail': 2 };
    var overallRank  = 0;
    for (var vi = 0; vi < allVerdicts.length; vi++) {
        var r = verdictRank2[allVerdicts[vi]] || 0;
        if (r > overallRank) overallRank = r;
    }
    var overallVerdict = ['pass', 'warning', 'fail'][overallRank] || 'pass';

    // Speed trend: linear regression x=log2(nominal_ms), y=avg_deviation_stops
    // Positive slope → bias toward fast (runs fast at fast speeds)
    var trendStr;
    if (trendPoints.length < 2) {
        var ns = trendPoints.length;
        trendStr = 'Insufficient data (only ' + ns + ' speed' + (ns === 1 ? '' : 's') + ' tested)';
    } else {
        var slope = _linearRegressionSlope(trendPoints);
        if (slope === null) slope = 0;
        var tSign = _signPrefix(slope);
        if (slope > 0.05) {
            trendStr = 'Bias toward fast (' + tSign + slope.toFixed(2) + ' stops per doubling)';
        } else if (slope < -0.05) {
            trendStr = 'Bias toward slow (' + slope.toFixed(2) + ' stops per doubling)';
        } else {
            trendStr = 'Neutral (no systematic speed trend) (' + tSign + slope.toFixed(2) + ' stops per doubling)';
        }
    }

    // Speed range: groupOrder is fastest-first
    var speedRangeStr = groupOrder.length === 0 ? '\u2014'
                      : groupOrder.length === 1 ? _formatSpeedLabel(groupOrder[0])
                      : _formatSpeedLabel(groupOrder[groupOrder.length - 1]) + '\u2013' + _formatSpeedLabel(groupOrder[0]);

    // --- Build markdown ---
    var md = '';
    md += '# Shutter Tester Diagnostic Report\n';
    md += '**Generated:** ' + now + '\n\n';

    md += '## Camera & Session Info\n';
    md += '- **Camera:** ' + (sessionData.camera || 'Unknown') + '\n';
    md += '- **Total Measurements:** ' + totalMeasurements + '\n';
    if (sessionData.notes) {
        md += '- **Notes:** ' + sessionData.notes.replace(/\n/g, ' ') + '\n';
    }
    var metaLines = (sessionData.meta || []).map(function(m) {
        return '- **' + m.label + ':** ' + sessionFormatMetaValue(m.label, m.value);
    }).join('\n');
    if (metaLines) md += metaLines + '\n';
    md += '\n';

    md += '## Verdict Summary\n';
    md += '**Overall Status:** ' + _verdictLabel(overallVerdict) + '\n\n';
    var concernedSpeeds = speedStats.filter(function(s) { return s.verdict !== 'pass'; });
    if (concernedSpeeds.length > 0) {
        md += '\u26a0\ufe0f **Speed(s) of Concern:**\n';
        for (var ci = 0; ci < concernedSpeeds.length; ci++) {
            var cs = concernedSpeeds[ci];
            md += '- ' + _formatSpeedLabel(cs.label) + ': ' + _verdictLabel(cs.verdict) + '\n';
        }
        md += '\n';
    }
    md += '---\n\n';

    md += '## Measurement Details by Speed\n\n';
    for (var si = 0; si < speedStats.length; si++) {
        var stat = speedStats[si];
        var dSign  = _signPrefix(stat.avgDevPct);
        var stSign = _signPrefix(stat.avgDevStops);
        md += '### ' + _promptEscapeMd(_formatSpeedLabel(stat.label)) + '\n';
        md += '**Measurements:** ' + stat.count + '  \n';
        md += '**Avg Duration:** ' + stat.avgDur.toFixed(2) + ' ms  \n';
        md += '**Deviation:** ' + dSign + stat.avgDevPct.toFixed(1) + '% (range: ' + stat.minDevPct.toFixed(1) + '%\u2013' + stat.maxDevPct.toFixed(1) + '%)  \n';
        md += '**Deviation (Stops):** ' + stSign + stat.avgDevStops.toFixed(2) + ' stops  \n';
        md += '**Repeatability (CV%):** ' + (stat.cv !== null ? stat.cv.toFixed(1) + '%' : '\u2014') + '  \n';
        md += '**Shot Spread %** (peak-to-peak across shots): ' + (stat.spread !== null ? stat.spread.toFixed(1) + '%' : '\u2014') + '  \n';
        md += '**Sensor Spread %** (within-shot, avg across shots): ' + (stat.sensorSpread !== null ? stat.sensorSpread.toFixed(1) + '%' : '\u2014') + '  \n';
        md += '**Frame Capping:** ' + (stat.frameCapping !== null ? (stat.frameCapping < 0.01 ? '<0.01 stops' : stat.frameCapping.toFixed(2) + ' stops') : '\u2014') + ' (estimated across ' + _FILM_DIAGONAL_MM.toFixed(1) + ' mm diagonal)  \n';
        md += '**Device Verdict:** ' + _promptVerdictIcon(stat.verdict) + ' ' + _verdictLabel(stat.verdict) + '  \n';
        md += '**Individual Durations (ms):** ' + stat.durations.map(function(d) { return d.toFixed(2); }).join(', ');
        if (stat.nominalMs > 0) md += ' (target: ' + stat.nominalMs.toFixed(2) + ' ms)';
        md += '\n\n';

        md += '### Curtain edge timing (per measurement, averaged)\n';
        md += '| Metric | Value | Status |\n';
        md += '|--------|-------|--------|\n';
        md += '| 1st curtain edge time (10\u201390%) | '
            + (stat.avgCurtain1 !== null ? stat.avgCurtain1.toFixed(2) + ' ms' : '\u2014') + ' | \u2014 |\n';
        md += '| Dwell time | '
            + (stat.avgDwell !== null ? stat.avgDwell.toFixed(2) + ' ms' : '\u2014') + ' | \u2014 |\n';
        md += '| 2nd curtain edge time (90\u201310%) | '
            + (stat.avgCurtain2 !== null ? stat.avgCurtain2.toFixed(2) + ' ms' : '\u2014') + ' | \u2014 |\n';
        md += '| Curtain balance (closing \u00f7 opening) | '
            + (stat.avgCurtainRatio !== null ? stat.avgCurtainRatio.toFixed(2) : '\u2014') + ' | \u2014 |\n';
        var balanceRangeStr = '\u2014';
        if (stat.ratioMin !== null && stat.ratioMax !== null) {
            if (Math.abs(stat.ratioMax - stat.ratioMin) < 0.005) {
                balanceRangeStr = stat.ratioMin.toFixed(2);
            } else {
                balanceRangeStr = stat.ratioMin.toFixed(2) + ' \u2192 ' + stat.ratioMax.toFixed(2)
                    + ' (\u0394' + Math.round((stat.ratioMax - stat.ratioMin) * 100) + '%)';
            }
        }
        md += '| Curtain balance range (across sensors) | ' + balanceRangeStr + ' | \u2014 |\n';
        md += '\n';
    }

    md += '---\n\n';
    md += '## Trend Analysis\n';
    md += '**Speed Range Tested:** ' + speedRangeStr + '  \n';
    md += '**Trend:** ' + trendStr + '\n\n';

    md += '---\n\n';
    md += '## Device-Configured Verdict Thresholds\n\n';
    md += 'The device applied these thresholds to generate the verdicts above. The verdict is driven solely by absolute deviation in stops; capping, spread, and repeatability are reported for diagnostic context but do not affect pass/warning/fail.\n\n';
    if (_verdictThresholds && _verdictThresholds.deviation) {
        var vt = _verdictThresholds;
        md += '| Metric | Warning | Fail |\n';
        md += '|--------|---------|------|\n';
        md += '| Deviation | >' + vt.deviation.warning + ' stops | >' + vt.deviation.fail + ' stops |\n\n';
    } else {
        md += 'No threshold data available from device.\n\n';
    }

    md += '## Measurement Notes\n\n';
    md += '- **Curtain edge times** are measured using the 10%\u201390% excursion algorithm on the stored waveform: the time between the curtain edge crossing the 10% and 90% transmission points at a sensor.\n';
    md += '- **Curtain balance** (closing \u00f7 opening curtain edge time): 1.00 = perfectly balanced (ideal). Values consistently away from 1.00 across multiple speeds suggest curtain spring imbalance \u2014 a common CLA target. No automated threshold applied.\n';
    md += '- **Curtain balance range (across sensors)**: spread of the per-sensor balance ratio within the same shot. A large \u0394 here indicates the curtains travel at different speeds across the gate (taper/skew), distinct from a uniform curtain-imbalance offset.\n';
    md += '- **Shot Spread %** = peak-to-peak of avg_duration_ms across shots at the same speed (shot-to-shot repeatability).\n';
    md += '- **Sensor Spread %** = within-shot percentage spread of duration across sensors, averaged over shots (gate-uniformity proxy; secondary verdict input).\n';
    md += '- **Dwell time**: interval between end of 1st curtain and start of 2nd curtain (fully-open period).\n';
    md += '- **Frame capping**: estimated exposure gradient across the full 35mm diagonal (' + _FILM_DIAGONAL_MM.toFixed(1) + ' mm).\n';
    md += '\n---\n\n';

    md += '## Analysis Instructions\n\n';
    md += 'You have the raw data and individual measurements above. Please form your own assessment:\n\n';
    md += '1. **Independent Tolerance Assessment:** Based on the raw measurements, is this shutter within acceptable limits for typical photographic use? You may agree with or override the device verdicts.\n';
    md += '2. **Wear & Maintenance Patterns:** Do the metrics suggest wear (asymmetry, slow trend), lubrication issues, or age-related drift (rising spread at slow speeds)?\n';
    md += '3. **Recommendations:** If issues are present, what maintenance or adjustment might restore performance? (e.g., cleaning, lubrication, professional service)\n';
    md += '4. **Comparative Context:** If you are familiar with similar cameras or shutter types, how does this performance compare?\n\n';
    md += 'The device verdicts are one interpretation. Focus on diagnostic insights from the raw data \u2014 your expertise on camera mechanics is the real value here.\n';

    return md;
}

// ============================================================================
// AI Diagnostic Prompt — clipboard handler
// ============================================================================

function sessionCopyAiPrompt() {
    var data = _sessionsCurrentData;
    if (!data) {
        showMessage('No session data loaded.', 'error');
        return;
    }
    var prompt;
    try {
        prompt = generateDiagnosticPrompt(data);
    } catch (e) {
        console.error('[sessions] generateDiagnosticPrompt error:', e);
        showMessage('Error generating prompt. Please try again.', 'error');
        return;
    }
    if (!prompt) {
        console.error('[sessions] generateDiagnosticPrompt returned empty');
        showMessage('Error generating prompt. Please try again.', 'error');
        return;
    }
    if (navigator.clipboard && navigator.clipboard.writeText) {
        navigator.clipboard.writeText(prompt).then(function() {
            showMessage('Copied to clipboard \u2014 paste into ChatGPT, Claude, etc.', 'success');
        }).catch(function(err) {
            console.error('[sessions] Clipboard API failed:', err);
            _copyFallback(prompt);
        });
    } else {
        console.warn('[sessions] Clipboard API unavailable (non-HTTPS context), using fallback');
        _copyFallback(prompt);
    }
}

function _copyFallback(text) {
    var ta = document.createElement('textarea');
    ta.value = text;
    ta.style.position = 'fixed';
    ta.style.left = '-9999px';
    document.body.appendChild(ta);
    ta.select();
    try {
        var ok = document.execCommand('copy');
        if (ok) {
            showMessage('Copied to clipboard \u2014 paste into ChatGPT, Claude, etc.', 'success');
        } else {
            console.error('[sessions] execCommand copy returned false');
            showMessage('Copy failed. Try manual copy (Ctrl+C).', 'error');
        }
    } catch (e) {
        console.error('[sessions] execCommand copy error:', e);
        showMessage('Copy failed. Try manual copy (Ctrl+C).', 'error');
    } finally {
        document.body.removeChild(ta);
    }
}
