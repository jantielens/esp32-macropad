// portal_brews_templates.js - Brew template management and detail view
// Extracted from portal_brews.js for maintainability.

// ============================================================================
// Brew Templates
// ============================================================================

async function brewTmplLoad() {
    try {
        const resp = await fetch(API_BREW_TEMPLATES);
        if (!resp.ok) throw new Error('Failed to load templates');
        const templates = await resp.json();
        brewTmplRender(templates);
    } catch (err) {
        console.error('brewTmplLoad error:', err);
    }
}

function brewTmplRender(templates) {
    const countEl = document.getElementById('brew-tmpl-count');
    if (countEl) countEl.textContent = '(' + templates.length + ')';

    const container = document.getElementById('brew-tmpl-cards');
    if (!container) return;
    container.innerHTML = '';

    templates.forEach((t, idx) => {
        const card = document.createElement('div');
        card.className = 'brew-tmpl-card';
        card.style.animationDelay = (idx * 0.05) + 's';

        const badgeClass = t.is_dynamic ? 'brew-tmpl-badge-custom' : 'brew-tmpl-badge-builtin';
        const badgeText = t.is_dynamic ? 'Custom' : 'Built-in';
        const desc = (t.description || '') + (t.description ? ' \u00b7 ' : '') + t.stage_count + ' stage' + (t.stage_count !== 1 ? 's' : '');

        let actionsHtml = '<button class="brew-tmpl-dl" title="Download">\u2B07\uFE0F</button>';
        if (t.is_dynamic) {
            actionsHtml += '<button class="brew-tmpl-del" title="Delete">\uD83D\uDDD1\uFE0F</button>';
        }

        card.innerHTML =
            '<div class="brew-tmpl-card-info">' +
                '<div class="brew-tmpl-card-title">' +
                    (t.display_name || t.name) +
                    ' <span class="brew-tmpl-badge ' + badgeClass + '">' + badgeText + '</span>' +
                '</div>' +
                '<div class="brew-tmpl-card-desc">' + desc + '</div>' +
            '</div>' +
            '<div class="brew-tmpl-card-actions">' + actionsHtml + '</div>';

        card.querySelector('.brew-tmpl-dl').addEventListener('click', (e) => { e.stopPropagation(); brewTmplDownload(t.name); });
        const delBtn = card.querySelector('.brew-tmpl-del');
        if (delBtn) delBtn.addEventListener('click', (e) => { e.stopPropagation(); brewTmplDelete(t.name, t.display_name || t.name); });

        card.style.cursor = 'pointer';
        card.addEventListener('click', () => brewTmplShowDetail(t.name));

        container.appendChild(card);
    });
}

async function brewTmplDownload(name) {
    try {
        const resp = await fetch(API_BREW_TEMPLATES + '/get?name=' + encodeURIComponent(name));
        if (!resp.ok) throw new Error('Download failed');
        const text = await resp.text();
        const blob = new Blob([text], { type: 'application/json' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = name + '.json';
        a.click();
        URL.revokeObjectURL(url);
    } catch (err) {
        console.error('brewTmplDownload error:', err);
        if (typeof showMessage === 'function') showMessage('Download failed', 'error');
    }
}

async function brewTmplDelete(name, displayName) {
    if (!confirm('Delete template "' + displayName + '"?')) return;
    try {
        const resp = await fetch(API_BREW_TEMPLATES + '?name=' + encodeURIComponent(name), { method: 'DELETE' });
        if (!resp.ok) {
            const data = await resp.json().catch(() => null);
            throw new Error(data && data.error ? data.error : 'Delete failed');
        }
        const result = await resp.json();
        const msg = result.status === 'reset_to_default' ? 'Deleted override — reset to built-in' : 'Template deleted';
        if (typeof showMessage === 'function') showMessage(msg, 'success');
        brewTmplLoad();
    } catch (err) {
        console.error('brewTmplDelete error:', err);
        if (typeof showMessage === 'function') showMessage(err.message || 'Delete failed', 'error');
    }
}

// ============================================================================
// Brew Template Detail
// ============================================================================

let brewTmplDetailData = null;  // cached full template JSON

const EFFECT_ICONS = {
    tare: '⚖️ tare',
    beep: '🔔 beep',
    capture_dose: '📝 dose',
    capture_weight: '📦 capture',
    marker: '📍 marker'
};

const TYPE_LABELS = {
    manual: 'Manual',
    auto_weight: 'Auto Weight',
    auto_time: 'Auto Time'
};

async function brewTmplShowDetail(name) {
    try {
        const resp = await fetch(API_BREW_TEMPLATES + '/get?name=' + encodeURIComponent(name));
        if (!resp.ok) throw new Error('Failed to load template');
        brewTmplDetailData = await resp.json();

        const t = brewTmplDetailData;
        const isDynamic = !!t.is_dynamic;

        // Header
        document.getElementById('brew-tmpl-detail-name').textContent = '☕ ' + (t.display_name || t.name);
        const badge = document.getElementById('brew-tmpl-detail-badge');
        badge.textContent = isDynamic ? 'Custom' : 'Built-in';
        badge.className = 'brew-tmpl-badge ' + (isDynamic ? 'brew-tmpl-badge-custom' : 'brew-tmpl-badge-builtin');

        // Description
        const descEl = document.getElementById('brew-tmpl-detail-desc');
        descEl.textContent = t.description || '';
        descEl.style.display = t.description ? '' : 'none';

        // Meta
        const metaParts = [];
        metaParts.push(t.stages.length + ' stage' + (t.stages.length !== 1 ? 's' : ''));
        if (t.start_label) metaParts.push('Start: "' + t.start_label + '"');
        if (t.done_label) metaParts.push('Done: "' + t.done_label + '"');
        document.getElementById('brew-tmpl-detail-meta').textContent = metaParts.join('  \u00b7  ');

        // Stages
        brewTmplRenderStages(t.stages);

        // Raw JSON
        document.getElementById('brew-tmpl-detail-json').textContent = JSON.stringify(t, null, 2);

        // Action buttons
        const actionsEl = document.getElementById('brew-tmpl-detail-actions');
        actionsEl.innerHTML = '';
        const dlBtn = document.createElement('button');
        dlBtn.type = 'button';
        dlBtn.className = 'btn btn-secondary';
        dlBtn.textContent = 'Download';
        dlBtn.addEventListener('click', () => brewTmplDownload(t.name));
        actionsEl.appendChild(dlBtn);
        if (isDynamic) {
            const delBtn = document.createElement('button');
            delBtn.type = 'button';
            delBtn.className = 'btn btn-danger';
            delBtn.textContent = 'Delete';
            delBtn.addEventListener('click', () => {
                brewTmplDelete(t.name, t.display_name || t.name);
                brewTmplBackToList();
            });
            actionsEl.appendChild(delBtn);
        }

        // Toggle views
        var el;
        el = document.getElementById('brew-tmpl-list-view');
        if (el) el.style.display = 'none';
        el = document.getElementById('brew-tmpl-detail-view');
        if (el) el.style.display = 'block';
        window.scrollTo(0, 0);
    } catch (err) {
        console.error('brewTmplShowDetail error:', err);
        if (typeof showMessage === 'function') showMessage('Failed to load template', 'error');
    }
}

function brewTmplRenderStages(stages) {
    const container = document.getElementById('brew-tmpl-stages');
    container.innerHTML = '';

    stages.forEach((s, idx) => {
        const el = document.createElement('div');
        el.className = 'brew-tmpl-stage';
        el.style.animationDelay = (idx * 0.05) + 's';

        const typeClass = 'brew-tmpl-type-' + (s.type || 'manual');
        const typeLabel = TYPE_LABELS[s.type] || s.type || 'Manual';

        // Build pills
        let pillsHtml = '';
        if (s.target_weight) pillsHtml += '<span class="brew-tmpl-pill">' + s.target_weight + 'g target</span>';
        if (s.target_flow_rate) pillsHtml += '<span class="brew-tmpl-pill">' + s.target_flow_rate + ' g/s flow</span>';
        if (s.auto_time_s) pillsHtml += '<span class="brew-tmpl-pill">' + s.auto_time_s + 's duration</span>';
        if (s.auto_threshold) pillsHtml += '<span class="brew-tmpl-pill">trigger &gt;' + s.auto_threshold + 'g</span>';
        if (s.capture) pillsHtml += '<span class="brew-tmpl-pill">' + (s.capture.label || s.capture.key) + ' (' + (s.capture.unit || 'g') + ')</span>';
        if (s.beep_pattern) pillsHtml += '<span class="brew-tmpl-pill">\uD83D\uDD0A ' + s.beep_pattern + '</span>';
        if (s.countdown_beep) pillsHtml += '<span class="brew-tmpl-pill">\u23F0 ' + s.countdown_beep + '</span>';
        if (s.countdown_done_beep) pillsHtml += '<span class="brew-tmpl-pill">\u23F0\u2714 ' + s.countdown_done_beep + '</span>';
        if (s.weight_cue_g) {
          let cueLabel = '\u2696\uFE0F ' + s.weight_cue_g + 'g';
          if (s.weight_cue_times && s.weight_cue_times > 1) cueLabel += ' \u00D7' + s.weight_cue_times;
          if (s.weight_cue_beep) cueLabel += ' ' + s.weight_cue_beep;
          pillsHtml += '<span class="brew-tmpl-pill">' + cueLabel + '</span>';
        }
        if (s.weight_done_beep) pillsHtml += '<span class="brew-tmpl-pill">\u2696\u2714 ' + s.weight_done_beep + '</span>';

        // Enter/exit effects
        let effectsHtml = '';
        if (s.on_enter && s.on_enter.length) {
            s.on_enter.forEach(e => {
                effectsHtml += '<span class="brew-tmpl-effect-tag">' + (EFFECT_ICONS[e] || e) + ' \u2B07</span>';
            });
        }
        if (s.on_exit && s.on_exit.length) {
            s.on_exit.forEach(e => {
                effectsHtml += '<span class="brew-tmpl-effect-tag">' + (EFFECT_ICONS[e] || e) + ' \u2B06</span>';
            });
        }

        let html = '<span class="brew-tmpl-stage-num">' + (idx + 1) + '</span>';
        html += '<div class="brew-tmpl-stage-header">';
        html += '<span class="brew-tmpl-stage-name">' + (s.name || 'Stage ' + (idx + 1)) + '</span>';
        html += '<span class="brew-tmpl-type-badge ' + typeClass + '">' + typeLabel + '</span>';
        html += '</div>';

        if (pillsHtml || effectsHtml) {
            html += '<div class="brew-tmpl-stage-pills">' + effectsHtml + pillsHtml + '</div>';
        }

        if (s.instruction) {
            html += '<div class="brew-tmpl-stage-instruction">' + s.instruction + '</div>';
        }

        if (s.next_label) {
            html += '<div class="brew-tmpl-stage-btn-label">[' + s.next_label + ' \u25B8]</div>';
        }

        el.innerHTML = html;
        container.appendChild(el);
    });
}

function brewTmplBackToList() {
    var el;
    el = document.getElementById('brew-tmpl-detail-view');
    if (el) el.style.display = 'none';
    el = document.getElementById('brew-tmpl-list-view');
    if (el) el.style.display = '';
}

async function brewTmplUpload(file) {
    const text = await file.text();

    // Client-side JSON validation
    try {
        const data = JSON.parse(text);
        if (!data.name || !data.stages) {
            if (typeof showMessage === 'function') showMessage('Invalid template: missing name or stages', 'error');
            return;
        }
    } catch (e) {
        if (typeof showMessage === 'function') showMessage('Invalid JSON file', 'error');
        return;
    }

    try {
        const resp = await fetch(API_BREW_TEMPLATES, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: text
        });
        if (!resp.ok) {
            const data = await resp.json().catch(() => null);
            throw new Error(data && data.error ? data.error : 'Upload failed');
        }
        if (typeof showMessage === 'function') showMessage('Template uploaded', 'success');
        brewTmplLoad();
    } catch (err) {
        console.error('brewTmplUpload error:', err);
        if (typeof showMessage === 'function') showMessage(err.message || 'Upload failed', 'error');
    }
}

