// portal_binding_help.js - Binding reference dialog built from GET /api/bindings?docs=1.
// Part of the ESP32 Macropad configuration portal.

const BINDING_HELP_CATEGORY_ORDER = ['Data', 'Device', 'Time', 'Logic'];

const BINDING_HELP_NUMBER_FORMATS = [
    ['%.0f', '21', 'Round to a whole number', '[expr:[health:cpu];%.0f]'],
    ['%.1f', '21.4', 'One decimal', '[mqtt:sensors/state;temperature;%.1f]'],
    ['%.2f', '21.37', 'Two decimals', '[mqtt:energy;cost;%.2f]'],
    ['%d', '42', 'Integer', '[health:rssi;%d]'],
    ['%05d', '00042', 'Zero-padded, fixed width', '[health:uptime;%05d]'],
    ['%+.1f', '+1.5', 'Always show the sign', '[mqtt:grid;delta;%+.1f]'],
    ['%.1f \u00b0C', '21.4 \u00b0C', 'Unit inside the format', '[mqtt:sensors/state;temperature;%.1f \u00b0C]'],
    ['%d%%', '73%', 'Literal % sign', '[health:cpu;%d%%]'],
    ['%s', 'text', 'Text as-is', '[mqtt:printer;state;%s]']
];

const BINDING_HELP_GUIDES = [
    { id: 'basics', title: 'Basics' },
    { id: 'format', title: 'Format strings' }
];

const _bindingHelp = {
    schemes: null,
    loading: null,
    error: '',
    view: 'guide:basics',
    target: null,
    targetFocused: false,
    pointer: null,
    opener: null
};

function _bhEsc(value) {
    return String(value == null ? '' : value).replace(/[&<>"']/g, c => (
        { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

// Key docs use '#' for digits and '*' for an id segment.
function bindingHelpKeyMatches(pattern, key) {
    let source = '^';
    for (const c of pattern) {
        if (c === '#') source += '[0-9]+';
        else if (c === '*') source += '[^.;]+';
        else source += c.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    }
    return new RegExp(source + '$').test(key);
}

function bindingHelpDisplayKey(pattern) {
    return pattern.replace(/#/g, 'N').replace(/\*/g, 'id');
}

function bindingHelpConcreteKeys(scheme, pattern) {
    if (!/[#*]/.test(pattern)) return [pattern];
    return (scheme.keys || []).filter(key => bindingHelpKeyMatches(pattern, key));
}

function bindingHelpKeyToken(scheme, pattern) {
    const concrete = bindingHelpConcreteKeys(scheme, pattern);
    const key = concrete.length ? concrete[0] : pattern.replace(/#/g, '1').replace(/\*/g, 'id');
    return '[' + scheme.name + ':' + key + ']';
}

function bindingHelpSignature(scheme) {
    const params = (scheme.params || []).map(p => p.required ? p.name : p.name + '?');
    return '[' + scheme.name + ':' + params.join(';') + ']';
}

function bindingHelpCategories(schemes) {
    const groups = new Map();
    schemes.forEach(scheme => {
        const category = scheme.category || 'Other';
        if (!groups.has(category)) groups.set(category, []);
        groups.get(category).push(scheme);
    });
    const rank = name => {
        const index = BINDING_HELP_CATEGORY_ORDER.indexOf(name);
        return index < 0 ? BINDING_HELP_CATEGORY_ORDER.length : index;
    };
    return Array.from(groups.entries()).sort((a, b) => rank(a[0]) - rank(b[0]) || a[0].localeCompare(b[0]));
}

// Higher is better; names beat descriptions and word starts beat substrings.
function bindingHelpScore(query, primary, secondary) {
    const name = String(primary || '').toLowerCase();
    const text = String(secondary || '').toLowerCase();
    if (name === query) return 100;
    if (name.startsWith(query)) return 80;
    const wordStart = value => value.split(/[^a-z0-9%]+/).some(word => word.startsWith(query));
    if (wordStart(name)) return 60;
    if (name.includes(query)) return 40;
    if (wordStart(text)) return 20;
    if (text.includes(query)) return 10;
    return 0;
}

function bindingHelpFormatRows(schemes) {
    const groups = [{ title: 'Numbers', scheme: '', rows: BINDING_HELP_NUMBER_FORMATS.map(r => (
        { syntax: r[0], sample: r[1], desc: r[2], example: r[3] })) }];
    schemes.forEach(scheme => {
        if (scheme.reference && scheme.reference.formats) {
            groups.push({ title: scheme.reference.title, scheme: scheme.name, rows: scheme.reference.rows });
        }
    });
    return groups;
}

function bindingHelpSearch(schemes, rawQuery) {
    const query = String(rawQuery || '').trim().toLowerCase();
    if (!query) return [];
    const results = [];
    const formatHits = [];
    bindingHelpFormatRows(schemes).forEach(group => group.rows.forEach(row => {
        const score = bindingHelpScore(query, row.syntax, [row.desc, row.sample, row.example, group.title].join(' '));
        if (score) formatHits.push({ score, kind: 'reference', row });
    }));
    if (formatHits.length) results.push({ scheme: null, title: 'Format strings', hits: formatHits });

    schemes.forEach(scheme => {
        const hits = [];
        const schemeScore = bindingHelpScore(query, scheme.name, [scheme.summary, scheme.category].join(' '));
        if (schemeScore) hits.push({ score: schemeScore, kind: 'summary' });
        (scheme.key_docs || []).forEach(doc => {
            const score = bindingHelpScore(query, doc.key, [doc.desc, doc.group].join(' '));
            if (score) hits.push({ score, kind: 'key', doc });
        });
        if (scheme.reference && !scheme.reference.formats) {
            scheme.reference.rows.forEach(row => {
                const score = bindingHelpScore(query, row.syntax, [row.desc, row.example, row.group].join(' '));
                if (score) hits.push({ score, kind: 'reference', row });
            });
        }
        (scheme.examples || []).forEach(example => {
            const score = Math.min(30, bindingHelpScore(query, example.code, example.desc));
            if (score) hits.push({ score, kind: 'example', example });
        });
        if (hits.length) results.push({ scheme, title: scheme.name, hits });
    });
    results.forEach(group => {
        group.hits.sort((a, b) => b.score - a.score);
        group.best = group.hits[0].score;
    });
    return results.sort((a, b) => b.best - a.best);
}

function _bhHighlight(text, query) {
    const value = String(text || '');
    const index = query ? value.toLowerCase().indexOf(query) : -1;
    if (index < 0) return _bhEsc(value);
    return _bhEsc(value.slice(0, index)) + '<mark>' + _bhEsc(value.slice(index, index + query.length)) +
        '</mark>' + _bhEsc(value.slice(index + query.length));
}

function _bhActions(token) {
    const code = _bhEsc(token);
    let html = '<button type="button" class="binding-copy-btn" data-bh-copy="' + code + '">Copy</button>';
    if (_bindingHelp.target) {
        html += '<button type="button" class="binding-copy-btn binding-insert-btn" data-bh-insert="' + code + '">Insert</button>';
    }
    return html;
}

function _bhKeyRow(scheme, doc, query) {
    const concrete = doc.key.indexOf('*') >= 0 ? bindingHelpConcreteKeys(scheme, doc.key) : [];
    let detail = _bhHighlight(doc.desc, query);
    if (concrete.length && concrete.length <= 12) {
        detail += '<span class="binding-docs-available">' + concrete.map(_bhEsc).join(', ') + '</span>';
    }
    return '<tr><td class="binding-docs-key"><code>' + _bhHighlight(bindingHelpDisplayKey(doc.key), query) +
        '</code></td><td>' + detail + '</td><td class="binding-docs-actions">' +
        _bhActions(bindingHelpKeyToken(scheme, doc.key)) + '</td></tr>';
}

function _bhReferenceTable(rows, query, showSample) {
    let html = '<table class="binding-docs-table"><tbody>';
    let group = null;
    rows.forEach(row => {
        if (row.group && row.group !== group) {
            group = row.group;
            html += '<tr class="binding-docs-group"><td colspan="' + (showSample ? 4 : 3) + '">' + _bhEsc(group) + '</td></tr>';
        }
        html += '<tr><td class="binding-docs-key"><code>' + _bhHighlight(row.syntax, query) + '</code></td>';
        if (showSample) html += '<td class="binding-docs-sample">' + _bhEsc(row.sample || '') + '</td>';
        html += '<td>' + _bhHighlight(row.desc, query);
        if (row.example && !showSample) html += '<br><code class="binding-docs-inline">' + _bhEsc(row.example) + '</code>';
        html += '</td><td class="binding-docs-actions">' + (row.example ? _bhActions(row.example) : '') + '</td></tr>';
    });
    return html + '</tbody></table>';
}

function _bhExamples(examples, query) {
    return '<div class="binding-example-grid">' + examples.map(example =>
        '<article class="binding-example"><pre class="binding-example-code"><code>' +
        _bhHighlight(example.code, query) + '</code></pre><p>' + _bhHighlight(example.desc, query) +
        '</p><div class="binding-example-actions">' + _bhActions(example.code) + '</div></article>').join('') + '</div>';
}

function _bhRenderScheme(scheme) {
    let html = '<div class="binding-docs-title"><h4><code>' + _bhEsc(scheme.name) + '</code></h4>' +
        '<span class="binding-docs-badge">' + _bhEsc(scheme.category || 'Other') + '</span></div>' +
        '<p class="binding-docs-summary">' + _bhEsc(scheme.summary) + '</p>';
    if (scheme.status) {
        html += '<p class="binding-docs-status" role="status">' + _bhEsc(scheme.status) +
            '. Tokens show --- or their fallback until this is resolved.</p>';
    }
    html += '<pre class="binding-docs-signature"><code>' + _bhEsc(bindingHelpSignature(scheme)) + '</code></pre>';
    html += '<section><h5>Parameters</h5><table class="binding-docs-table"><tbody>' +
        (scheme.params || []).map(p => '<tr><td class="binding-docs-key"><code>' + _bhEsc(p.name) +
            '</code> <span class="binding-docs-req">' + (p.required ? 'required' : 'optional') +
            '</span></td><td>' + _bhEsc(p.desc) + '</td></tr>').join('') + '</tbody></table></section>';
    if (scheme.note) html += '<p class="binding-docs-note">' + _bhEsc(scheme.note) + '</p>';
    const keys = scheme.key_docs || [];
    if (keys.length) {
        html += '<section><h5>Keys <span class="binding-docs-muted">(' + keys.length + ')</span>' +
            (keys.length > 8 ? '<input type="search" class="form-control form-control-sm binding-docs-key-filter" placeholder="Filter keys" aria-label="Filter keys" data-bh-key-filter>' : '') +
            '</h5><table class="binding-docs-table"><tbody data-bh-keys>' + _bhKeyRows(scheme, '') + '</tbody></table></section>';
    }
    if (scheme.reference && scheme.reference.rows && scheme.reference.rows.length) {
        html += '<section><h5>' + _bhEsc(scheme.reference.title) + '</h5>' +
            _bhReferenceTable(scheme.reference.rows, '', scheme.reference.formats) + '</section>';
    }
    if ((scheme.examples || []).length) html += '<section><h5>Examples</h5>' + _bhExamples(scheme.examples, '') + '</section>';
    return html;
}

function _bhKeyRows(scheme, filter) {
    let html = '';
    let group = null;
    (scheme.key_docs || []).forEach(doc => {
        if (filter && (doc.key + ' ' + doc.desc + ' ' + (doc.group || '')).toLowerCase().indexOf(filter) < 0) return;
        if (doc.group && doc.group !== group) {
            group = doc.group;
            html += '<tr class="binding-docs-group"><td colspan="3">' + _bhEsc(group) + '</td></tr>';
        }
        html += _bhKeyRow(scheme, doc, filter);
    });
    return html || '<tr><td colspan="3" class="binding-docs-empty">No matching keys</td></tr>';
}

function _bhRenderGuide(id) {
    const schemes = _bindingHelp.schemes || [];
    if (id === 'format') {
        return '<h4>Format strings</h4><p class="binding-docs-summary">Schemes with a <code>format</code> parameter accept ' +
            'printf-style codes. Put units inside the format. Some schemes accept only float or only integer codes; ' +
            'see each scheme\'s format parameter.</p>' +
            bindingHelpFormatRows(schemes).map(group => '<section><h5>' + _bhEsc(group.title) +
                (group.scheme ? ' <span class="binding-docs-muted">' + _bhEsc(group.scheme) + ' scheme</span>' : '') +
                '</h5>' + _bhReferenceTable(group.rows, '', true) + '</section>').join('');
    }
    return '<h4>Basics</h4>' +
        '<p class="binding-docs-summary">A binding is a <code>[scheme:params]</code> token that resolves to live data. ' +
        'Use bindings in labels, colors, button state, widget inputs, and many action fields. Text around tokens is kept, ' +
        'and up to 4 tokens fit in one field. Only bindings available on this device are listed.</p>' +
        '<section><h5>Fallback</h5><p>Append <code>|value</code> to replace the default <code>---</code> while a binding ' +
        'has no value yet, for example <code>[mqtt:solar/power;watts|0]</code>. In color fields, use a valid color: ' +
        '<code>[mqtt:light;hex|#f8fafc]</code>.</p></section>' +
        '<section><h5>Nesting</h5><p>Tokens can be nested inside <code>[expr:...]</code>, for example ' +
        '<code>[expr:[health:cpu] &gt; 80 ? "High" : "OK"]</code>. Inner tokens resolve first.</p></section>' +
        '<section><h5>Widget data</h5><p>Widget data fields need a number, so leave out the format parameter. ' +
        'Table and List widgets take one structured token with no text around it, such as <code>[health:table]</code>.</p></section>';
}

function _bhRenderSearch(query) {
    const q = query.trim().toLowerCase();
    const groups = bindingHelpSearch(_bindingHelp.schemes || [], q);
    let count = 0;
    const html = groups.map(group => {
        let body = '';
        const keys = group.hits.filter(hit => hit.kind === 'key');
        const refs = group.hits.filter(hit => hit.kind === 'reference');
        const examples = group.hits.filter(hit => hit.kind === 'example');
        if (group.scheme && group.hits.some(hit => hit.kind === 'summary')) {
            body += '<p class="binding-docs-summary">' + _bhHighlight(group.scheme.summary, q) + '</p>';
        }
        if (keys.length) body += '<table class="binding-docs-table"><tbody>' + keys.map(hit => _bhKeyRow(group.scheme, hit.doc, q)).join('') + '</tbody></table>';
        if (refs.length) body += _bhReferenceTable(refs.map(hit => hit.row), q, !group.scheme);
        if (examples.length) body += _bhExamples(examples.map(hit => hit.example), q);
        count += group.hits.length;
        const title = group.scheme
            ? '<button type="button" class="binding-docs-link" data-bh-view="' + _bhEsc(group.scheme.name) + '"><code>' + _bhEsc(group.scheme.name) + '</code></button>'
            : '<button type="button" class="binding-docs-link" data-bh-view="guide:format">Format strings</button>';
        return '<section class="binding-docs-hits"><h5>' + title + '</h5>' + body + '</section>';
    }).join('');
    document.getElementById('binding-docs-count').textContent = count === 1 ? '1 match' : count + ' matches';
    return html || '<p class="binding-docs-empty">No bindings on this device match \u201c' + _bhEsc(query.trim()) + '\u201d.</p>';
}

function _bhRenderNav() {
    const nav = document.getElementById('binding-docs-nav');
    if (!nav) return;
    let html = '<h6>Guides</h6>' + BINDING_HELP_GUIDES.map(guide =>
        '<button type="button" data-bh-view="guide:' + guide.id + '">' + _bhEsc(guide.title) + '</button>').join('');
    bindingHelpCategories(_bindingHelp.schemes || []).forEach(entry => {
        html += '<h6>' + _bhEsc(entry[0]) + '</h6><div class="binding-docs-nav-group">' + entry[1].map(scheme => {
            const keyCount = (scheme.keys || scheme.key_docs || []).length;
            return '<button type="button" data-bh-view="' + _bhEsc(scheme.name) + '"><code>' + _bhEsc(scheme.name) + '</code>' +
                (scheme.status ? '<span class="binding-docs-dot" title="' + _bhEsc(scheme.status) + '"></span>' : '') +
                (keyCount ? '<span class="binding-docs-muted">' + keyCount + '</span>' : '') + '</button>';
        }).join('') + '</div>';
    });
    nav.innerHTML = html;
    _bhMarkNav();
}

function _bhMarkNav() {
    document.querySelectorAll('#binding-docs-nav [data-bh-view]').forEach(button => {
        const active = button.dataset.bhView === _bindingHelp.view;
        button.classList.toggle('active', active);
        if (active) {
            button.setAttribute('aria-current', 'true');
            if (typeof button.scrollIntoView === 'function') button.scrollIntoView({ block: 'nearest', inline: 'nearest' });
        } else {
            button.removeAttribute('aria-current');
        }
    });
}

function _bhRender() {
    const body = document.getElementById('binding-docs-body');
    const search = document.getElementById('binding-docs-search');
    const count = document.getElementById('binding-docs-count');
    if (!body) return;
    if (_bindingHelp.error) {
        body.innerHTML = '<p class="binding-docs-empty">' + _bhEsc(_bindingHelp.error) +
            ' <button type="button" class="binding-docs-link" data-bh-retry>Retry</button></p>';
        return;
    }
    if (!_bindingHelp.schemes) {
        body.innerHTML = '<p class="binding-docs-empty">Loading binding reference\u2026</p>';
        return;
    }
    const query = search ? search.value : '';
    if (query.trim()) {
        body.innerHTML = _bhRenderSearch(query);
    } else {
        count.textContent = _bindingHelp.schemes.length + ' schemes';
        const scheme = _bindingHelp.schemes.find(s => s.name === _bindingHelp.view);
        body.innerHTML = scheme ? _bhRenderScheme(scheme) : _bhRenderGuide(_bindingHelp.view.replace('guide:', ''));
    }
    body.scrollTop = 0;
}

function _bhLoad() {
    if (_bindingHelp.schemes) return Promise.resolve();
    if (_bindingHelp.loading) return _bindingHelp.loading;
    _bindingHelp.error = '';
    _bindingHelp.loading = fetch('/api/bindings?docs=1', { cache: 'no-store' })
        .then(response => {
            if (!response.ok) throw new Error('HTTP ' + response.status);
            return response.json();
        })
        .then(payload => {
            _bindingHelp.schemes = Array.isArray(payload && payload.schemes) ? payload.schemes : [];
        })
        .catch(() => {
            _bindingHelp.error = 'Could not load the binding reference.';
        })
        .then(() => {
            _bindingHelp.loading = null;
            _bhRenderNav();
            _bhRender();
        });
    return _bindingHelp.loading;
}

function _bhIsTextField(element) {
    if (!element || element.disabled || element.readOnly) return false;
    if (element.tagName === 'TEXTAREA') return true;
    return element.tagName === 'INPUT' && ['text', 'search', ''].indexOf(element.type || '') >= 0;
}

// The text field an fx hint or ? button belongs to, if any.
function bindingHelpResolveTarget(trigger) {
    if (!trigger) return null;
    const label = trigger.closest('label');
    if (label && label.htmlFor) {
        const field = document.getElementById(label.htmlFor);
        if (_bhIsTextField(field)) return field;
    }
    let node = trigger.parentElement;
    for (let depth = 0; node && depth < 4; depth++, node = node.parentElement) {
        const fields = Array.from(node.querySelectorAll('input, textarea')).filter(_bhIsTextField);
        if (fields.length) return fields[0];
    }
    return null;
}

function showBindingHelp(section) {
    const overlay = document.getElementById('binding-help-overlay');
    if (!overlay) return;
    if (!_bindingHelp.target) _bindingHelp.opener = document.activeElement;
    if (section) _bindingHelp.view = section === 'overview' || section === 'fallback' || section === 'widget'
        ? 'guide:basics' : section;
    const search = document.getElementById('binding-docs-search');
    if (search) search.value = '';
    overlay.style.display = 'flex';
    _bhRenderNav();
    _bhRender();
    _bhLoad();
    if (search) search.focus();
}

function closeBindingHelp() {
    const overlay = document.getElementById('binding-help-overlay');
    if (!overlay) return;
    overlay.style.display = 'none';
    const restore = _bindingHelp.target || _bindingHelp.opener;
    _bindingHelp.target = null;
    _bindingHelp.opener = null;
    if (restore && restore.isConnected && typeof restore.focus === 'function') restore.focus();
}

function bindingHelpInsertText(field, text, focused) {
    const value = field.value || '';
    let start = value.length;
    let end = value.length;
    if (focused && typeof field.selectionStart === 'number') {
        start = field.selectionStart;
        end = field.selectionEnd;
    } else if (value && !/\s$/.test(value)) {
        text = ' ' + text;
    }
    field.value = value.slice(0, start) + text + value.slice(end);
    const caret = start + text.length;
    if (typeof field.setSelectionRange === 'function') field.setSelectionRange(caret, caret);
    field.dispatchEvent(new Event('input', { bubbles: true }));
    field.dispatchEvent(new Event('change', { bubbles: true }));
}

function _bhFlash(button, label) {
    const original = button.textContent;
    button.textContent = label;
    button.classList.add('is-copied');
    window.setTimeout(() => {
        button.textContent = original;
        button.classList.remove('is-copied');
    }, 1200);
}

document.addEventListener('pointerdown', event => {
    if (!event.target.closest || !event.target.closest('.fx-hint, .binding-help-btn, #cp-fx')) return;
    const active = document.activeElement;
    _bindingHelp.pointer = _bhIsTextField(active)
        ? { field: active, start: active.selectionStart, end: active.selectionEnd } : null;
}, true);

// Capture phase: record the target field before the trigger's own onclick opens the dialog.
document.addEventListener('click', event => {
    const trigger = event.target.closest && event.target.closest('.fx-hint, .binding-help-btn, #cp-fx');
    if (!trigger) return;
    const field = bindingHelpResolveTarget(trigger);
    const pointer = _bindingHelp.pointer;
    _bindingHelp.target = field;
    _bindingHelp.targetFocused = !!(field && pointer && pointer.field === field);
    if (_bindingHelp.targetFocused && typeof field.setSelectionRange === 'function') {
        field.setSelectionRange(pointer.start, pointer.end);
    }
    _bindingHelp.pointer = null;
}, true);

document.addEventListener('click', event => {
    const overlay = event.target.closest && event.target.closest('#binding-help-overlay');
    if (!overlay) return;
    const view = event.target.closest('[data-bh-view]');
    if (view) {
        _bindingHelp.view = view.dataset.bhView;
        document.getElementById('binding-docs-search').value = '';
        _bhMarkNav();
        _bhRender();
        return;
    }
    const copy = event.target.closest('[data-bh-copy]');
    if (copy) {
        copyTextToClipboard(copy.dataset.bhCopy)
            .then(() => _bhFlash(copy, 'Copied'))
            .catch(() => showMessage('Could not copy binding', 'error'));
        return;
    }
    const insert = event.target.closest('[data-bh-insert]');
    if (insert && _bindingHelp.target && _bindingHelp.target.isConnected) {
        bindingHelpInsertText(_bindingHelp.target, insert.dataset.bhInsert, _bindingHelp.targetFocused);
        closeBindingHelp();
        return;
    }
    if (event.target.closest('[data-bh-retry]')) {
        _bhRender();
        _bhLoad();
    }
});

document.addEventListener('input', event => {
    if (event.target.id === 'binding-docs-search') {
        _bhRender();
        return;
    }
    if (event.target.matches && event.target.matches('[data-bh-key-filter]')) {
        const scheme = (_bindingHelp.schemes || []).find(s => s.name === _bindingHelp.view);
        const rows = document.querySelector('#binding-docs-body [data-bh-keys]');
        if (scheme && rows) rows.innerHTML = _bhKeyRows(scheme, event.target.value.trim().toLowerCase());
    }
});

document.addEventListener('keydown', event => {
    const overlay = document.getElementById('binding-help-overlay');
    if (!overlay || overlay.style.display === 'none') return;
    const search = document.getElementById('binding-docs-search');
    const typing = event.target.matches && event.target.matches('input, textarea');
    if (event.key === '/' && !typing) {
        event.preventDefault();
        search.focus();
    } else if (event.key === 'Escape') {
        event.preventDefault();
        event.stopPropagation();
        if (search.value) {
            search.value = '';
            _bhRender();
            search.focus();
        } else {
            closeBindingHelp();
        }
    }
}, true);
