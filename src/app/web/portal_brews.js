// portal_brews.js - Brew log list, detail, charts, export/import
// Part of the ESP32 Macropad configuration portal.

const API_BREWS = '/api/brews';

let brewListData = null;      // cached list response
let brewDetailData = null;    // cached single brew (full)
let brewListScroll = 0;       // scroll position to restore on back
let weightChart = null;
let flowChart = null;

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
    const avgFlowEl = document.getElementById('stat-avg-flow');

    if (!brews || brews.length === 0) {
        countEl.textContent = '0';
        avgTimeEl.textContent = '--';
        avgWeightEl.textContent = '--';
        avgFlowEl.textContent = '--';
        return;
    }

    countEl.textContent = brews.length;

    let totalDur = 0, totalWeight = 0, totalFlow = 0;
    let durCount = 0, weightCount = 0, flowCount = 0;

    for (const brew of brews) {
        const dur = brewFindField(brew.fields, 'duration');
        const wt = brewFindField(brew.fields, 'water');
        const fl = brewFindField(brew.fields, 'avg_flow');
        if (dur) { totalDur += dur.value; durCount++; }
        if (wt) { totalWeight += wt.value; weightCount++; }
        if (fl) { totalFlow += fl.value; flowCount++; }
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
    avgFlowEl.textContent = flowCount > 0 ? (totalFlow / flowCount).toFixed(1) + ' g/s' : '--';
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
        const ts = brewFindField(brew.fields, 'ts');
        const dur = brewFindField(brew.fields, 'duration');
        const wt = brewFindField(brew.fields, 'water');
        const pf = brewFindField(brew.fields, 'peak_flow');

        const card = document.createElement('div');
        card.className = 'brew-card';
        card.style.animationDelay = (idx * 0.05) + 's';
        card.setAttribute('tabindex', '0');
        card.setAttribute('role', 'button');
        card.setAttribute('aria-label', `View brew: ${name ? name.value : 'Brew'}`);

        card.innerHTML = `
            <div class="brew-card-header">
                <span class="brew-card-name">☕ ${name ? name.value : 'Brew'}</span>
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
                <div class="brew-card-stat">
                    <span class="brew-card-stat-value">${pf ? brewFormatField(pf) : '--'}</span>
                    <span class="brew-card-stat-label">Peak Flow</span>
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
        document.getElementById('brew-detail-name').textContent = '☕ ' + (name ? name.value : 'Brew');
        document.getElementById('brew-detail-date').textContent = ts ? brewFormatField(ts) : '';

        // Fields grid (skip template and ts — shown in header)
        const fieldsEl = document.getElementById('brew-detail-fields');
        fieldsEl.innerHTML = '';
        for (const field of brewDetailData.fields) {
            if (field.key === 'template' || field.key === 'ts') continue;
            const div = document.createElement('div');
            div.className = 'brew-field';
            div.innerHTML = `<span class="brew-field-label">${field.label}</span>
                             <span class="brew-field-value">${brewFormatField(field)}</span>`;
            fieldsEl.appendChild(div);
        }

        // Charts
        brewCreateCharts(brewDetailData.series);

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

function brewBackToList() {
    // Destroy charts
    if (weightChart) { weightChart.destroy(); weightChart = null; }
    if (flowChart) { flowChart.destroy(); flowChart = null; }

    document.getElementById('brew-detail-view').style.display = 'none';
    document.getElementById('brew-list-view').style.display = 'block';
    window.scrollTo(0, brewListScroll);
}

// ============================================================================
// Charts (Chart.js)
// ============================================================================

function brewCreateCharts(series) {
    if (weightChart) { weightChart.destroy(); weightChart = null; }
    if (flowChart) { flowChart.destroy(); flowChart = null; }

    if (!window.Chart) {
        document.getElementById('brew-weight-fallback').style.display = 'block';
        document.getElementById('brew-flow-fallback').style.display = 'block';
        return;
    }

    document.getElementById('brew-weight-fallback').style.display = 'none';
    document.getElementById('brew-flow-fallback').style.display = 'none';

    if (!series || !series.weight || series.weight.length === 0) return;

    const intervalSec = (series.interval_ms || 1000) / 1000;
    const labels = series.weight.map((_, i) => (i * intervalSec).toFixed(0));

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
                }
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

    // Flow rate chart with segment coloring
    const flowCtx = document.getElementById('brew-flow-chart').getContext('2d');

    function flowColor(val) {
        if (val > 3.5) return '#EF5350';
        if (val > 2.5) return '#FFA726';
        if (val >= 1.5) return '#66BB6A';
        return '#808080';
    }

    flowChart = new Chart(flowCtx, {
        type: 'line',
        data: {
            labels: labels,
            datasets: [{
                label: 'Flow (g/s)',
                data: series.flow,
                segment: {
                    borderColor: (ctx) => {
                        const val = ctx.p1.parsed.y;
                        return flowColor(val);
                    }
                },
                borderColor: '#808080',
                fill: false,
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
                        label: (item) => item.parsed.y.toFixed(2) + ' g/s'
                    }
                }
            },
            scales: {
                x: {
                    title: { display: true, text: 'Time (s)' },
                    ticks: { maxTicksLimit: 10 }
                },
                y: {
                    title: { display: true, text: 'Flow (g/s)' },
                    beginAtZero: true
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
    const exportData = { v: brew.v, fields: brew.fields, series: brew.series };
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
                allBrews.push({ v: data.v, fields: data.fields, series: data.series });
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
