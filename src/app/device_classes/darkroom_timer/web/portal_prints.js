// Portal Prints — list + detail view for print session logs

// Device-class-owned HTML escaper. The shared portal_core.js exposes escAttr
// but not escHtml, so it is defined here to avoid touching shared files.
function escHtml(s) {
    const d = document.createElement('div');
    d.appendChild(document.createTextNode(s == null ? '' : s));
    return d.innerHTML;
}

function portalPrintsInit() {
    fetchPrintsList();

    document.getElementById('prints-back-btn').addEventListener('click', showListView);
    document.getElementById('prints-export-btn').addEventListener('click', exportAll);
    document.getElementById('prints-clear-btn').addEventListener('click', clearAll);
    document.getElementById('prints-delete-btn').addEventListener('click', deleteCurrent);
    document.getElementById('prints-save-notes-btn').addEventListener('click', saveNotes);
}

let currentPrintId = null;
let currentPrintType = null;
let currentPrintData = null;  // parsed detail data for Load action
let currentPrintStarred = false;
let starFilterActive = false; // client-side "starred only" filter

var SECTION_HEADING_STYLE = 'font-size:14px; color:#86868b; text-transform:uppercase; letter-spacing:0.5px';

// Convert fields array [{key, label, value, ...}] to keyed object {key: value}
function fieldsToMap(arr) {
    var m = {};
    if (!Array.isArray(arr)) return arr || {};
    for (var i = 0; i < arr.length; i++) {
        if (arr[i].key) m[arr[i].key] = arr[i].value;
    }
    return m;
}

// ============================================================================
// List view
// ============================================================================

function fetchPrintsList() {
    fetch('/api/prints')
        .then(r => r.json())
        .then(data => renderList(data))
        .catch(err => {
            document.getElementById('prints-list').innerHTML =
                '<p style="color:#ff3b30;">Failed to load prints.</p>';
        });
}

function renderList(data) {
    const listEl = document.getElementById('prints-list');
    const summaryEl = document.getElementById('prints-summary');
    const prints = data.prints || [];

    // Summary + star filter on one line
    var filterIcon = starFilterActive ? '\u2605' : '\u2606';
    var filterColor = starFilterActive ? '#FFD700' : '#86868b';
    summaryEl.innerHTML = prints.length + ' of ' + data.max + ' prints' +
        ' <span id="prints-star-filter" style="cursor:pointer; font-size:14px; color:' +
        filterColor + '; margin-left:8px; vertical-align:baseline;" title="Toggle starred filter">' +
        filterIcon + '</span>';

    if (prints.length === 0) {
        listEl.innerHTML = '<p style="color:#86868b;">No prints recorded yet.</p>';
        attachStarFilter(data);
        return;
    }

    // Apply client-side starred filter
    const visible = starFilterActive ? prints.filter(function(p) { return !!p.starred; }) : prints;

    let html = '';
    if (visible.length === 0) {
        html += '<p style="color:#86868b;">No starred prints.</p>';
    } else {
        html += '<div style="display:flex; flex-direction:column; gap:8px;">';
        for (const p of visible) {
            const f = fieldsToMap(p.fields);
            const type = f.type || '?';
            const icon = type === 'test_strip' ? '\ud83d\udd2c' : '\ud83d\udda8\ufe0f';
            const id = f.id || '?';
            const time = formatTime(f.set_time || f.base_time);
            const typeLabel = type === 'test_strip' ? 'Test Strip' : 'Exposure';
            const starPrefix = p.starred ? '<span style="color:#FFD700;">\u2605</span> ' : '';
            let summary = icon + ' ' + id + ' / ' + typeLabel + ' - ' + time;
            if (p.notes) {
                const firstLine = p.notes.split('\n')[0];
                summary += ' - ' + firstLine;
            }

            html += '<div class="prints-row" data-id="' + escAttr(id) + '" style="' +
                'padding:12px 16px; background:var(--portal-surface-alt); border-radius:10px; cursor:pointer; ' +
                'display:flex; justify-content:space-between; align-items:center; gap:12px;">' +
                '<div style="font-weight:600; font-size:14px; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; min-width:0;">' +
                (p.starred ? starPrefix : '') + escHtml(summary) + '</div>' +
                '<span style="font-size:18px; color:#86868b; flex-shrink:0;">\u203a</span>' +
                '</div>';
        }
        html += '</div>';
    }
    listEl.innerHTML = html;

    // Attach click handlers
    listEl.querySelectorAll('.prints-row').forEach(row => {
        row.addEventListener('click', () => openDetail(row.dataset.id));
    });

    attachStarFilter(data);
}

function attachStarFilter(data) {
    var filterEl = document.getElementById('prints-star-filter');
    if (filterEl) {
        filterEl.addEventListener('click', function() {
            starFilterActive = !starFilterActive;
            renderList(data);
        });
    }
}

function formatTime(s) {
    if (s == null) return '?';
    const n = parseFloat(s);
    return n >= 60 ? (n / 60).toFixed(1) + ' min' : n.toFixed(1) + 's';
}

// ============================================================================
// Detail view
// ============================================================================

function openDetail(id) {
    fetch('/api/prints?id=' + encodeURIComponent(id))
        .then(r => { if (!r.ok) throw new Error(r.status); return r.json(); })
        .then(data => renderDetail(data))
        .catch(err => alert('Failed to load print: ' + err.message));
}

// Field categorization
var METERING_KEYS = new Set(['lref', 'zone5_time', 'l_bright', 'l_dark', 'sbr', 'grade', 'grade_label', 'mag_factor']);
var HERO_SKIP = new Set(['id', 'type', 'ts', 'segment_count']);

function renderDetail(data) {
    const fields = fieldsToMap(data.fields);
    currentPrintId = fields.id || null;
    currentPrintType = fields.type || null;
    currentPrintData = fields;
    const fieldArr = Array.isArray(data.fields) ? data.fields : [];
    const isStrip = fields.type === 'test_strip';
    currentPrintStarred = !!data.starred;

    // --- Hero card ---
    var heroHtml = '<div style="background:#2c2c2e; border-radius:12px; padding:20px; color:#e5e5ea;">';
    heroHtml += '<div style="display:flex; justify-content:space-between; align-items:flex-start; margin-bottom:12px;">';
    heroHtml += '<div style="font-size:13px; color:#98989d;">' +
        escHtml((isStrip ? '\ud83d\udd2c ' : '\ud83d\udda8\ufe0f ') + (fields.id || '?') + ' \u00b7 ' + (isStrip ? 'Test Strip' : 'Exposure')) + '</div>';
    heroHtml += '<span id="detail-star-btn" style="font-size:22px; cursor:pointer; line-height:1;" title="Toggle star"></span>';
    heroHtml += '</div>';

    // Hero values — collect slots, skip missing
    var heroSlots = [];
    if (isStrip) {
        if (fields.base_time != null) heroSlots.push({ label: 'Base Time', value: formatTime(fields.base_time) });
        if (fields.step != null) heroSlots.push({ label: 'Step', value: String(fields.step) });
        if (fields.segment_count != null) heroSlots.push({ label: 'Segments', value: String(fields.segment_count) });
    } else {
        if (fields.effective_time != null) heroSlots.push({ label: 'Effective Time', value: formatTime(fields.effective_time) });
        if (fields.grade != null) {
            var gv = String(fields.grade);
            if (fields.grade_label) gv += ' · ' + fields.grade_label;
            heroSlots.push({ label: 'Grade', value: gv });
        }
        if (fields.dry_down != null) heroSlots.push({ label: 'Dry-Down', value: (fields.dry_down > 0 ? '+' : '') + parseFloat(fields.dry_down).toFixed(1) + '%' });
    }

    if (heroSlots.length > 0) {
        heroHtml += '<div style="display:flex; gap:24px; flex-wrap:wrap;">';
        for (var hs of heroSlots) {
            heroHtml += '<div style="min-width:80px;">' +
                '<div style="font-size:11px; color:#98989d; text-transform:uppercase; letter-spacing:0.5px; margin-bottom:4px;">' + escHtml(hs.label) + '</div>' +
                '<div style="font-size:22px; font-weight:700; color:#f5f5f7;">' + escHtml(hs.value) + '</div>' +
                '</div>';
        }
        heroHtml += '</div>';
    }

    // Date line
    if (fields.ts && fields.ts > 0) {
        heroHtml += '<div style="font-size:12px; color:#98989d; margin-top:12px;">' +
            escHtml(new Date(fields.ts * 1000).toLocaleString()) + '</div>';
    }
    heroHtml += '</div>';
    document.getElementById('detail-hero').innerHTML = heroHtml;
    updateStarBtn(document.getElementById('detail-star-btn'), currentPrintStarred);

    // --- Categorize fields into exposure and metering ---
    var skipInSections = new Set(['id', 'type', 'ts', 'segment_count']);
    // Also skip hero-promoted keys from exposure section to avoid duplication
    if (!isStrip) {
        skipInSections.add('effective_time');
        skipInSections.add('grade');
        skipInSections.add('grade_label');
        skipInSections.add('dry_down');
    } else {
        skipInSections.add('base_time');
        skipInSections.add('step');
        skipInSections.add('step_stops');
        skipInSections.add('segment_count');
    }

    var exposureFields = [];
    var meteringFields = [];
    for (var entry of fieldArr) {
        if (skipInSections.has(entry.key)) continue;
        if (METERING_KEYS.has(entry.key)) {
            meteringFields.push(entry);
        } else {
            exposureFields.push(entry);
        }
    }

    // --- Exposure section ---
    var expHtml = '';
    if (exposureFields.length > 0) {
        expHtml += '<h3 style="margin:0 0 8px 0; ' + SECTION_HEADING_STYLE + '">Exposure</h3>';
        expHtml += renderFieldTable(exposureFields);
    }
    document.getElementById('detail-exposure').innerHTML = expHtml;

    // --- Segments table (test strip only) ---
    var segDiv = document.getElementById('detail-segments');
    if (data.segments && data.segments.length > 0) {
        var segHtml = '<h3 style="margin:0 0 8px 0; ' + SECTION_HEADING_STYLE + '">Segments</h3>' +
            '<table style="width:100%; border-collapse:collapse; font-size:13px;">' +
            '<tr><th style="text-align:left; padding:6px 8px; border-bottom:1px solid #d1d1d6;">#</th>' +
            '<th style="text-align:left; padding:6px 8px; border-bottom:1px solid #d1d1d6;">Offset</th>' +
            '<th style="text-align:right; padding:6px 8px; border-bottom:1px solid #d1d1d6;">Incremental</th>' +
            '<th style="text-align:right; padding:6px 8px; border-bottom:1px solid #d1d1d6;">Cumulative</th></tr>';
        for (var i = 0; i < data.segments.length; i++) {
            var seg = data.segments[i];
            segHtml += '<tr><td style="padding:6px 8px;">' + seg.n + '</td>' +
                '<td style="padding:6px 8px;">' + escHtml(seg.offset) + '</td>' +
                '<td style="text-align:right; padding:6px 8px;">' + seg.incremental_s.toFixed(1) + 's</td>' +
                '<td style="text-align:right; padding:6px 8px;">' + seg.cumulative_s.toFixed(1) + 's</td></tr>';
        }
        segHtml += '</table>';
        segDiv.innerHTML = segHtml;
        segDiv.style.display = '';
    } else {
        segDiv.style.display = 'none';
    }

    // --- Metering context section ---
    var metHtml = '';
    if (meteringFields.length > 0) {
        metHtml += '<div style="background:#1c1c1e; border-radius:10px; padding:14px 16px;">';
        metHtml += '<h3 style="margin:0 0 4px 0; ' + SECTION_HEADING_STYLE + '">Metering Context</h3>';
        metHtml += '<p style="margin:0 0 10px 0; font-size:11px; color:#636366;">Meter readings at time of print</p>';
        metHtml += renderFieldTable(meteringFields, '#a1a1a6');
        metHtml += '</div>';
    }
    document.getElementById('detail-metering').innerHTML = metHtml;

    // --- Notes ---
    document.getElementById('detail-notes').value = data.notes || '';

    // Show detail, hide list
    document.getElementById('prints-list-view').style.display = 'none';
    document.getElementById('prints-detail-view').style.display = '';

    // Star toggle handler
    // Element recreated via innerHTML — no removeEventListener needed
    var starBtn = document.getElementById('detail-star-btn');
    if (starBtn) {
        starBtn.addEventListener('click', toggleDetailStar);
    }
}

function renderFieldTable(entries, color) {
    var c = color || '#1d1d1f';
    var rows = '';
    for (var entry of entries) {
        rows += '<tr><td style="padding:5px 12px 5px 0; font-weight:600; white-space:nowrap; font-size:13px; color:' + c + ';">' +
            escHtml(entry.label || entry.key) + '</td>' +
            '<td style="padding:5px 0; font-size:13px; color:' + c + ';">' +
            escHtml(formatFieldValue(entry.key, entry.value, entry.unit)) + '</td></tr>';
    }
    return '<table style="width:100%; border-collapse:collapse;">' + rows + '</table>';
}

function formatFieldValue(k, v, unit) {
    if (v == null || v === '') return '—';
    if (typeof v === 'number' && unit === 's') return v.toFixed(1) + 's';
    if (typeof v === 'number' && unit === '%') return v.toFixed(1) + '%';
    if (typeof v === 'number' && unit) return v + ' ' + unit;
    if (k === 'ts' && typeof v === 'number' && v > 0) {
        return new Date(v * 1000).toLocaleString();
    }
    return String(v);
}

// ============================================================================
// Actions
// ============================================================================

function showListView() {
    document.getElementById('prints-detail-view').style.display = 'none';
    document.getElementById('prints-list-view').style.display = '';
    currentPrintId = null;
    currentPrintType = null;
    currentPrintData = null;
    currentPrintStarred = false;
    fetchPrintsList();
}

function toggleDetailStar() {
    if (!currentPrintId) return;
    var newStarred = !currentPrintStarred;
    // Optimistic UI update
    currentPrintStarred = newStarred;
    var starBtn = document.getElementById('detail-star-btn');
    updateStarBtn(starBtn, newStarred);
    fetch('/api/prints?id=' + encodeURIComponent(currentPrintId), {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ starred: newStarred })
    })
        .then(function(r) { if (!r.ok) throw new Error(r.status); })
        .catch(function(err) {
            // Revert on failure
            currentPrintStarred = !newStarred;
            updateStarBtn(starBtn, !newStarred);
        });
}

function saveNotes() {
    if (!currentPrintId) return;
    const notes = document.getElementById('detail-notes').value.trim();
    fetch('/api/prints?id=' + encodeURIComponent(currentPrintId), {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ notes: notes })
    })
        .then(r => { if (!r.ok) throw new Error(r.status); return r.json(); })
        .then(() => { /* silently saved */ })
        .catch(err => alert('Failed to save notes: ' + err.message));
}

function deleteCurrent() {
    if (!currentPrintId) return;
    if (!confirm('Delete print ' + currentPrintId + '?')) return;
    fetch('/api/prints?id=' + encodeURIComponent(currentPrintId), { method: 'DELETE' })
        .then(r => { if (!r.ok) throw new Error(r.status); return r.json(); })
        .then(() => showListView())
        .catch(err => alert('Failed to delete: ' + err.message));
}

function clearAll() {
    if (!confirm('Delete ALL prints? This cannot be undone.')) return;
    fetch('/api/prints?confirm=true', { method: 'DELETE' })
        .then(r => { if (!r.ok) throw new Error(r.status); return r.json(); })
        .then(() => fetchPrintsList())
        .catch(err => alert('Failed to clear: ' + err.message));
}

function exportAll() {
    window.open('/api/prints/export', '_blank');
}

// ============================================================================
// Helpers
// ============================================================================

function updateStarBtn(btn, starred) {
    if (!btn) return;
    btn.textContent = starred ? '\u2605' : '\u2606';
    btn.style.color = starred ? '#FFD700' : '#636366';
}
