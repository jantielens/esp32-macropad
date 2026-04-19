// portal_config_actions.js - Swipe actions, boot actions, timer config, sound management
// Part of the ESP32 Macropad configuration portal.
// Bundled into portal_config.js during minification.

// ============================================================================
// Swipe Actions (uses shared portal_action_editor.js)
// ============================================================================

const SWIPE_DIRECTIONS = ['swipe-left', 'swipe-right', 'swipe-up', 'swipe-down'];
const SWIPE_LABELS = { 'swipe-right': 'Swipe Right', 'swipe-left': 'Swipe Left', 'swipe-up': 'Swipe Up', 'swipe-down': 'Swipe Down' };

function swipeInitEditors() {
    var container = document.getElementById('swipe-editors');
    if (!container) return;
    var html = '';
    SWIPE_DIRECTIONS.forEach(function(dir) {
        html += '<details class="editor-group" id="' + dir + '-group">';
        html += '<summary>' + SWIPE_LABELS[dir] + '</summary>';
        html += '<div class="editor-group-body">';
        html += actionEditorHTML(dir);
        html += '</div></details>';
    });
    container.innerHTML = html;
}

async function loadSwipeActions() {
    try {
        const response = await fetch('/api/swipe-actions');
        if (!response.ok) return;
        const data = await response.json();
        actionEditorLoad('swipe-left', data.swipe_left);
        actionEditorLoad('swipe-right', data.swipe_right);
        actionEditorLoad('swipe-up', data.swipe_up);
        actionEditorLoad('swipe-down', data.swipe_down);
    } catch (err) {
        console.error('Failed to load swipe actions:', err);
    }
}

// ---------------------------------------------------------------------------
// Sound file management
// ---------------------------------------------------------------------------
function soundEsc(s) {
    var d = document.createElement('span');
    d.textContent = s;
    return d.innerHTML;
}

async function loadSoundList() {
    const container = document.getElementById('sound-list');
    if (!container) return;
    try {
        const resp = await fetch('/api/sounds/list');
        if (!resp.ok) { container.innerHTML = '<small style="color:#86868b;">Could not load sounds.</small>'; return; }
        const names = await resp.json();
        if (!names.length) {
            container.innerHTML = '<small style="color:#86868b;">No sound files uploaded yet.</small>';
            return;
        }
        let html = '<table style="width:100%; border-collapse:collapse; font-size:13px;">';
        html += '<tr style="border-bottom:1px solid #e5e5ea;"><th style="text-align:left;padding:4px 8px;">Name</th><th style="width:120px;"></th></tr>';
        names.forEach(function(name) {
            html += '<tr style="border-bottom:1px solid #f0f0f0;">';
            html += '<td style="padding:6px 8px;"><code>' + soundEsc(name) + '</code></td>';
            html += '<td style="text-align:right; padding:4px 8px; white-space:nowrap;">';
            html += '<button type="button" class="btn" style="font-size:12px; padding:2px 10px; margin-right:4px;" onclick="playSound(\'' + soundEsc(name) + '\')">\u25B6 Play</button>';
            html += '<button type="button" class="btn" style="font-size:12px; padding:2px 10px; color:#ff3b30;" onclick="deleteSound(\'' + soundEsc(name) + '\')">&times; Delete</button>';
            html += '</td></tr>';
        });
        html += '</table>';
        container.innerHTML = html;
    } catch (err) {
        container.innerHTML = '<small style="color:#ff3b30;">Error loading sounds.</small>';
    }
}

async function uploadSound() {
    const nameInput = document.getElementById('sound-upload-name');
    const fileInput = document.getElementById('sound-upload-file');
    const statusEl = document.getElementById('sound-upload-status');
    const name = (nameInput.value || '').trim();
    if (!name || !/^[a-zA-Z0-9_-]+$/.test(name)) {
        statusEl.innerHTML = '<span style="color:#ff3b30;">Invalid name (letters, digits, _ and - only).</span>';
        return;
    }
    if (!fileInput.files || !fileInput.files.length) {
        statusEl.innerHTML = '<span style="color:#ff3b30;">Select an MP3 file first.</span>';
        return;
    }
    const file = fileInput.files[0];
    if (file.size > 512 * 1024) {
        statusEl.innerHTML = '<span style="color:#ff3b30;">File too large (max 512 KB).</span>';
        return;
    }
    const btn = document.getElementById('sound-upload-btn');
    btn.disabled = true;
    statusEl.innerHTML = '<span style="color:#007aff;">Uploading…</span>';
    try {
        const resp = await fetch('/api/sounds/upload?name=' + encodeURIComponent(name), {
            method: 'POST',
            headers: { 'Content-Type': 'application/octet-stream', 'Content-Length': file.size },
            body: file
        });
        if (resp.ok) {
            statusEl.innerHTML = '<span style="color:#34c759;">Uploaded successfully.</span>';
            nameInput.value = '';
            fileInput.value = '';
            loadSoundList();
        } else {
            const err = await resp.json().catch(function() { return {}; });
            statusEl.innerHTML = '<span style="color:#ff3b30;">' + soundEsc(err.error || 'Upload failed') + '</span>';
        }
    } catch (err) {
        statusEl.innerHTML = '<span style="color:#ff3b30;">Upload error: ' + soundEsc(err.message) + '</span>';
    } finally {
        btn.disabled = false;
    }
}

function soundFileSelected(input) {
    if (!input.files || !input.files.length) return;
    var nameInput = document.getElementById('sound-upload-name');
    if (nameInput && !nameInput.value) {
        var raw = input.files[0].name.replace(/\.mp3$/i, '');
        nameInput.value = raw.replace(/[^a-zA-Z0-9_-]/g, '-').replace(/-+/g, '-').replace(/^-|-$/g, '').substring(0, 31);
    }
}

async function playSound(name) {
    try {
        await fetch('/api/sounds/play?name=' + encodeURIComponent(name), { method: 'POST' });
    } catch (err) { /* ignore */ }
}

async function deleteSound(name) {
    if (!confirm('Delete sound "' + name + '"?')) return;
    try {
        const resp = await fetch('/api/sounds?name=' + encodeURIComponent(name), { method: 'DELETE' });
        if (resp.ok) loadSoundList();
    } catch (err) { /* ignore */ }
}

async function saveSwipeActions() {
    const payload = {
        swipe_left: actionEditorBuild('swipe-left'),
        swipe_right: actionEditorBuild('swipe-right'),
        swipe_up: actionEditorBuild('swipe-up'),
        swipe_down: actionEditorBuild('swipe-down')
    };
    try {
        const response = await fetch('/api/swipe-actions', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });
        if (response.ok) {
            showMessage('Swipe actions saved', 'success');
        } else {
            showMessage('Failed to save swipe actions', 'error');
        }
    } catch (err) {
        console.error('Error saving swipe actions:', err);
        showMessage('Error saving swipe actions: ' + err.message, 'error');
    }
}

// ============================================================================
// Boot Actions
// ============================================================================

const BOOT_ACTION_PREFIXES = ['boot-action-1', 'boot-action-2', 'boot-action-3'];
const BOOT_ACTION_LABELS = { 'boot-action-1': 'Action 1', 'boot-action-2': 'Action 2', 'boot-action-3': 'Action 3' };

function bootActionsInitEditors() {
    var container = document.getElementById('boot-action-editors');
    if (!container) return;
    var html = '';
    BOOT_ACTION_PREFIXES.forEach(function(prefix) {
        html += '<details class="editor-group" id="' + prefix + '-group">';
        html += '<summary>' + BOOT_ACTION_LABELS[prefix] + '</summary>';
        html += '<div class="editor-group-body">';
        html += actionEditorHTML(prefix);
        html += '</div></details>';
    });
    container.innerHTML = html;
}

async function loadBootActions() {
    try {
        const response = await fetch('/api/boot-actions');
        if (!response.ok) return;
        const data = await response.json();
        var actions = data.actions || [];
        BOOT_ACTION_PREFIXES.forEach(function(prefix, i) {
            actionEditorLoad(prefix, actions[i] || {});
        });
    } catch (err) {
        console.error('Failed to load boot actions:', err);
    }
}

async function saveBootActions() {
    var actions = [];
    BOOT_ACTION_PREFIXES.forEach(function(prefix) {
        actions.push(actionEditorBuild(prefix));
    });
    // Trim trailing empty actions
    while (actions.length > 0 && !actions[actions.length - 1].type) {
        actions.pop();
    }
    try {
        const response = await fetch('/api/boot-actions', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ actions: actions })
        });
        if (response.ok) {
            showMessage('Boot actions saved', 'success');
        } else {
            showMessage('Failed to save boot actions', 'error');
        }
    } catch (err) {
        console.error('Error saving boot actions:', err);
        showMessage('Error saving boot actions: ' + err.message, 'error');
    }
}

// ============================================================================
// Timer Config (device-level, uses shared portal_action_editor.js)
// ============================================================================

const TIMER_IDS = [1, 2, 3];
const TIMER_EXPIRE_PREFIXES = [];
for (var _ti = 1; _ti <= 3; _ti++) {
    for (var _ai = 1; _ai <= 3; _ai++) {
        TIMER_EXPIRE_PREFIXES.push('timer-' + _ti + '-expire-' + _ai);
    }
}

function timerConfigInitEditors() {
    var container = document.getElementById('timer-config-editors');
    if (!container) return;
    var html = '';
    TIMER_IDS.forEach(function(tid) {
        html += '<details class="editor-group" id="timer-' + tid + '-group">';
        html += '<summary>Timer ' + tid + '</summary>';
        html += '<div class="editor-group-body">';
        // Mode dropdown
        html += '<div class="form-group">';
        html += '<label for="timer-' + tid + '-mode">Mode</label>';
        html += '<select id="timer-' + tid + '-mode" onchange="timerModeChanged(' + tid + ')">';
        html += '<option value="up">Stopwatch (Count Up)</option>';
        html += '<option value="down">Countdown</option>';
        html += '</select>';
        html += '</div>';
        // Countdown duration (visible only in countdown mode)
        html += '<div class="form-group" id="timer-' + tid + '-countdown-group" style="display:none;">';
        html += '<label for="timer-' + tid + '-countdown">Countdown Duration (seconds)</label>';
        html += '<input type="number" id="timer-' + tid + '-countdown" min="1" max="86400" placeholder="e.g. 300">';
        html += '<small>Duration in seconds. The timer will count down from this value.</small>';
        html += '</div>';
        // Expire actions (visible only in countdown mode)
        html += '<div id="timer-' + tid + '-expire-section" style="display:none;">';
        html += '<h4 style="margin: 12px 0 8px;">On Expire Actions</h4>';
        html += '<small style="display:block; margin-bottom:12px;">Actions to run when the countdown reaches zero (e.g. play a sound, send MQTT message).</small>';
        for (var ai = 1; ai <= 3; ai++) {
            var prefix = 'timer-' + tid + '-expire-' + ai;
            html += '<details class="editor-group" id="' + prefix + '-group">';
            html += '<summary>Action ' + ai + '</summary>';
            html += '<div class="editor-group-body">';
            html += actionEditorHTML(prefix);
            html += '</div></details>';
        }
        html += '</div>';
        html += '</div></details>';
    });
    container.innerHTML = html;
}

function timerModeChanged(tid) {
    var modeEl = document.getElementById('timer-' + tid + '-mode');
    if (!modeEl) return;
    var isDown = (modeEl.value === 'down');
    var cdGrp = document.getElementById('timer-' + tid + '-countdown-group');
    var expSec = document.getElementById('timer-' + tid + '-expire-section');
    if (cdGrp) cdGrp.style.display = isDown ? '' : 'none';
    if (expSec) expSec.style.display = isDown ? '' : 'none';
}

async function loadTimerConfig() {
    try {
        const response = await fetch('/api/timers');
        if (!response.ok) return;
        const data = await response.json();
        TIMER_IDS.forEach(function(tid) {
            var tcfg = data[String(tid)] || {};
            var modeEl = document.getElementById('timer-' + tid + '-mode');
            if (modeEl) modeEl.value = tcfg.mode || 'up';
            var cdEl = document.getElementById('timer-' + tid + '-countdown');
            if (cdEl) cdEl.value = (tcfg.countdown > 0) ? tcfg.countdown : '';
            var expireActions = tcfg.expire_actions || [];
            for (var ai = 1; ai <= 3; ai++) {
                var prefix = 'timer-' + tid + '-expire-' + ai;
                actionEditorLoad(prefix, expireActions[ai - 1] || {});
            }
            timerModeChanged(tid);
        });
    } catch (err) {
        console.error('Failed to load timer config:', err);
    }
}

async function saveTimerConfig() {
    var payload = {};
    TIMER_IDS.forEach(function(tid) {
        var modeEl = document.getElementById('timer-' + tid + '-mode');
        var cdEl = document.getElementById('timer-' + tid + '-countdown');
        var tcfg = { mode: modeEl ? modeEl.value : 'up' };
        if (tcfg.mode === 'down' && cdEl && cdEl.value !== '' && parseInt(cdEl.value, 10) > 0) {
            tcfg.countdown = parseInt(cdEl.value, 10);
        }
        // Collect expire actions
        var actions = [];
        for (var ai = 1; ai <= 3; ai++) {
            var a = actionEditorBuild('timer-' + tid + '-expire-' + ai);
            if (a.type) actions.push(a);
        }
        if (actions.length > 0) tcfg.expire_actions = actions;
        payload[String(tid)] = tcfg;
    });
    try {
        const response = await fetch('/api/timers', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });
        if (response.ok) {
            showMessage('Timer config saved', 'success');
        } else {
            showMessage('Failed to save timer config', 'error');
        }
    } catch (err) {
        console.error('Error saving timer config:', err);
        showMessage('Error saving timer config: ' + err.message, 'error');
    }
}
