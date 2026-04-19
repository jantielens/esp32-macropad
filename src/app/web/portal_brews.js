// portal_brews.js - Brew log list, detail, charts, export/import
// Part of the ESP32 Macropad configuration portal.

const API_BREWS = '/api/brews';
const API_BREW_TEMPLATES = '/api/brew-templates';

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
    const { id, ...exportData } = brew;
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
                const { id, ...item } = data;
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

    // Wire template upload button
    document.getElementById('brew-tmpl-upload-btn').addEventListener('click', () => {
        document.getElementById('brew-tmpl-upload-file').click();
    });
    document.getElementById('brew-tmpl-upload-file').addEventListener('change', (e) => {
        const file = e.target.files[0];
        if (file) {
            brewTmplUpload(file);
            e.target.value = '';
        }
    });

    // Wire template detail back button
    document.getElementById('brew-tmpl-back-btn').addEventListener('click', brewTmplBackToList);

    // Load brew templates and brew list
    brewTmplLoad();
    brewLoadList();
});
