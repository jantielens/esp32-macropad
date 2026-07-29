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
        const response = await fetch('/api/component/swipe-actions/config');
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
        const response = await fetch('/api/component/swipe-actions/config', {
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
const BOOT_ACTION_LABELS = ['Action 1', 'Action 2', 'Action 3'];

function bootActionsInitEditors() {
    actionEditorListRender('boot-action-editors', BOOT_ACTION_PREFIXES, BOOT_ACTION_LABELS);
}

async function loadBootActions() {
    try {
        const response = await fetch('/api/component/boot-actions/config');
        if (!response.ok) return;
        const data = await response.json();
        actionEditorListLoad(BOOT_ACTION_PREFIXES, data.actions || []);
    } catch (err) {
        console.error('Failed to load boot actions:', err);
    }
}

async function saveBootActions() {
    var actions = actionEditorListBuild(BOOT_ACTION_PREFIXES);
    try {
        const response = await fetch('/api/component/boot-actions/config', {
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
// Hardware Button Actions (uses shared portal_action_editor.js)
// ============================================================================

// Flat list of every action editor prefix currently rendered, used to wire
// screen + sound dropdowns in one pass after the editors are built.
var HW_BUTTON_PREFIXES = [];

function hwButtonTapPrefixes(n) {
    return ['hwbtn-' + n + '-tap-1', 'hwbtn-' + n + '-tap-2', 'hwbtn-' + n + '-tap-3'];
}
function hwButtonHoldPrefixes(n) {
    return ['hwbtn-' + n + '-hold-1', 'hwbtn-' + n + '-hold-2', 'hwbtn-' + n + '-hold-3'];
}

async function initHwButtons() {
    var container = document.getElementById('hw-button-editors');
    if (!container) return;
    HW_BUTTON_PREFIXES = [];
    var data;
    try {
        const response = await fetch('/api/component/hw-buttons/config');
        if (!response.ok) {
            container.innerHTML = '<small style="color:#86868b;">Could not load hardware buttons.</small>';
            return;
        }
        data = await response.json();
    } catch (err) {
        console.error('Failed to load hardware buttons:', err);
        container.innerHTML = '<small style="color:#ff3b30;">Error loading hardware buttons.</small>';
        return;
    }

    var buttons = (data && data.buttons) || [];
    if (!buttons.length) {
        container.innerHTML = '<small style="color:#86868b;">This board has no configurable hardware buttons.</small>';
        return;
    }

    // Show the headless note when the device reports no display.
    if (typeof getDeviceInfo === 'function') {
        getDeviceInfo().then(function (info) {
            var note = document.getElementById('hw-button-headless-note');
            if (note && info && !info.has_display) note.style.display = '';
        });
    }

    var html = '';
    buttons.forEach(function (btn, idx) {
        var n = idx + 1;
        var label = (btn.label && btn.label.length) ? btn.label : ('Button ' + n);
        var title = 'Button ' + n + ' — ' + label + ' (GPIO' + btn.pin + ')';
        html += '<details class="editor-group" id="hwbtn-' + n + '-group">';
        html += '<summary>' + title + '</summary>';
        html += '<div class="editor-group-body">';
        html += '<h4 class="mt-2 mb-1">Tap Actions</h4>';
        html += '<div id="hwbtn-' + n + '-tap-editors"></div>';
        html += '<h4 class="mt-2 mb-1">Hold Actions</h4>';
        html += '<div id="hwbtn-' + n + '-hold-editors"></div>';
        html += '</div></details>';
    });
    container.innerHTML = html;

    var listLabels = ['Action 1', 'Action 2', 'Action 3'];
    buttons.forEach(function (btn, idx) {
        var n = idx + 1;
        var tapPrefixes = hwButtonTapPrefixes(n);
        var holdPrefixes = hwButtonHoldPrefixes(n);
        actionEditorListRender('hwbtn-' + n + '-tap-editors', tapPrefixes, listLabels);
        actionEditorListRender('hwbtn-' + n + '-hold-editors', holdPrefixes, listLabels);
        actionEditorListLoad(tapPrefixes, btn.tap_actions || []);
        actionEditorListLoad(holdPrefixes, btn.hold_actions || []);
        HW_BUTTON_PREFIXES = HW_BUTTON_PREFIXES.concat(tapPrefixes, holdPrefixes);
    });

    actionEditorWireFragment(HW_BUTTON_PREFIXES);
}

async function saveHwButtons() {
    var container = document.getElementById('hw-button-editors');
    if (!container) return;
    var groupCount = container.querySelectorAll('details.editor-group').length;
    var buttons = [];
    for (var n = 1; n <= groupCount; n++) {
        buttons.push({
            tap_actions: actionEditorListBuild(hwButtonTapPrefixes(n)),
            hold_actions: actionEditorListBuild(hwButtonHoldPrefixes(n))
        });
    }
    try {
        const response = await fetch('/api/component/hw-buttons/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ buttons: buttons })
        });
        if (response.ok) {
            showMessage('Hardware button actions saved', 'success');
        } else {
            showMessage('Failed to save hardware button actions', 'error');
        }
    } catch (err) {
        console.error('Error saving hardware button actions:', err);
        showMessage('Error saving hardware button actions: ' + err.message, 'error');
    }
}

// ============================================================================
// MQTT Triggers (uses shared portal_action_editor.js)
// ============================================================================

// Flat list of every action editor prefix currently rendered, used to wire
// screen + sound dropdowns in one pass after the editors are built.
var MQTT_TRIGGER_PREFIXES = [];

function mqttTriggerPrefixes(n) {
    return ['mqtttrig-' + n + '-1', 'mqtttrig-' + n + '-2', 'mqtttrig-' + n + '-3'];
}

async function initMqttTriggers() {
    var container = document.getElementById('mqtt-trigger-editors');
    if (!container) return;
    MQTT_TRIGGER_PREFIXES = [];
    var data;
    try {
        const response = await fetch('/api/component/mqtt-triggers/config');
        if (!response.ok) {
            container.innerHTML = '<small style="color:#86868b;">Could not load MQTT triggers.</small>';
            return;
        }
        data = await response.json();
    } catch (err) {
        console.error('Failed to load MQTT triggers:', err);
        container.innerHTML = '<small style="color:#ff3b30;">Error loading MQTT triggers.</small>';
        return;
    }

    var max = (data && data.max) || 8;
    var triggers = (data && data.triggers) || [];

    var html = '';
    for (var n = 1; n <= max; n++) {
        html += '<details class="editor-group" id="mqtttrig-' + n + '-group">';
        html += '<summary>Trigger ' + n + '</summary>';
        html += '<div class="editor-group-body">';
        html += '<label class="form-label">Topic</label>';
        html += '<input type="text" id="mqtttrig-' + n + '-topic" class="form-control form-control-sm mb-1" maxlength="127" spellcheck="false" placeholder="home/sensor/state">';
        html += '<label class="form-label">Value filter (empty = match any)</label>';
        html += '<input type="text" id="mqtttrig-' + n + '-value" class="form-control form-control-sm mb-2" maxlength="63" spellcheck="false" placeholder="e.g. ON">';
        html += '<h4 class="mt-2 mb-1">Actions</h4>';
        html += '<div id="mqtttrig-' + n + '-editors"></div>';
        html += '</div></details>';
    }
    container.innerHTML = html;

    var listLabels = ['Action 1', 'Action 2', 'Action 3'];
    for (var n = 1; n <= max; n++) {
        var prefixes = mqttTriggerPrefixes(n);
        actionEditorListRender('mqtttrig-' + n + '-editors', prefixes, listLabels);
        var trig = triggers[n - 1];
        if (trig) {
            var topicEl = document.getElementById('mqtttrig-' + n + '-topic');
            var valueEl = document.getElementById('mqtttrig-' + n + '-value');
            if (topicEl) topicEl.value = trig.topic || '';
            if (valueEl) valueEl.value = trig.value || '';
            actionEditorListLoad(prefixes, trig.actions || []);
            // Expand configured slots so they are visible on load.
            var grp = document.getElementById('mqtttrig-' + n + '-group');
            if (grp && trig.topic) grp.open = true;
        }
        MQTT_TRIGGER_PREFIXES = MQTT_TRIGGER_PREFIXES.concat(prefixes);
    }

    actionEditorWireFragment(MQTT_TRIGGER_PREFIXES);
}

async function saveMqttTriggers() {
    var container = document.getElementById('mqtt-trigger-editors');
    if (!container) return;
    var groupCount = container.querySelectorAll('details.editor-group').length;
    var triggers = [];
    for (var n = 1; n <= groupCount; n++) {
        var topicEl = document.getElementById('mqtttrig-' + n + '-topic');
        var valueEl = document.getElementById('mqtttrig-' + n + '-value');
        var topic = topicEl ? topicEl.value.trim() : '';
        if (!topic) continue;  // skip empty slots
        if (topic.indexOf('#') !== -1 || topic.indexOf('+') !== -1) {
            showMessage('Wildcard topics are not supported. Use exact topic names.', 'error');
            return;
        }
        triggers.push({
            topic: topic,
            value: valueEl ? valueEl.value : '',
            actions: actionEditorListBuild(mqttTriggerPrefixes(n))
        });
    }
    try {
        const response = await fetch('/api/component/mqtt-triggers/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ triggers: triggers })
        });
        if (response.ok) {
            showMessage('MQTT triggers saved', 'success');
        } else {
            var msg = 'Failed to save MQTT triggers';
            try { var j = await response.json(); if (j && j.message) msg = j.message; } catch (e) {}
            showMessage(msg, 'error');
        }
    } catch (err) {
        console.error('Error saving MQTT triggers:', err);
        showMessage('Error saving MQTT triggers: ' + err.message, 'error');
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
        html += '<div id="timer-' + tid + '-expire-section">';
        html += '<h4 style="margin: 12px 0 8px;">On Expire Actions</h4>';
        html += '<small style="display:block; margin-bottom:12px;">Actions snapshotted when a countdown starts and run once when it reaches zero.</small>';
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

async function loadTimerConfig() {
    try {
        const response = await fetch('/api/component/timers/config');
        if (!response.ok) return;
        const data = await response.json();
        TIMER_IDS.forEach(function(tid) {
            var tcfg = data[String(tid)] || {};
            var expireActions = tcfg.expire_actions || [];
            for (var ai = 1; ai <= 3; ai++) {
                var prefix = 'timer-' + tid + '-expire-' + ai;
                actionEditorLoad(prefix, expireActions[ai - 1] || {});
            }
        });
    } catch (err) {
        console.error('Failed to load timer config:', err);
    }
}

async function saveTimerConfig() {
    var payload = {};
    TIMER_IDS.forEach(function(tid) {
        var tcfg = { expire_actions: [] };
        var actions = [];
        for (var ai = 1; ai <= 3; ai++) {
            var a = actionEditorBuild('timer-' + tid + '-expire-' + ai);
            if (a.type) actions.push(a);
        }
        tcfg.expire_actions = actions;
        payload[String(tid)] = tcfg;
    });
    try {
        const response = await fetch('/api/component/timers/config', {
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
