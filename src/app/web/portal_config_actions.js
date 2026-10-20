// portal_config_actions.js - Swipe actions, boot actions, timer config, sound management
// Part of the ESP32 Macropad configuration portal.
// Bundled into portal_config.js during minification.

// ============================================================================
// Swipe Actions (uses shared portal_action_editor.js)
// ============================================================================

const SWIPE_DIRECTIONS = ['swipe-left', 'swipe-right', 'swipe-up', 'swipe-down'];
const SWIPE_LABELS = { 'swipe-right': 'Swipe Right', 'swipe-left': 'Swipe Left', 'swipe-up': 'Swipe Up', 'swipe-down': 'Swipe Down' };

async function swipeInitEditors() {
    var container = document.getElementById('swipe-editors');
    if (!container) return;
    // The action-type picker renders from the firmware catalog cached on
    // deviceInfoCache; wait for it before building any action editor markup.
    await getDeviceInfo();
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
            container.innerHTML = '<small style="color:#86868b;">No alert sounds uploaded yet.</small>';
            return;
        }
        let html = '';
        names.forEach(function(name) {
            html += '<div class="list-group-item" style="display:grid;grid-template-columns:minmax(0,1fr) auto auto;align-items:center;gap:12px;">';
            html += '<code style="overflow-wrap:anywhere;">' + soundEsc(name) + '</code>';
            html += '<button type="button" class="btn btn-secondary btn-sm" title="Play ' + soundEsc(name) + '" aria-label="Play ' + soundEsc(name) + '" onclick="playSound(\'' + soundEsc(name) + '\')"><span class="portal-icon" aria-hidden="true">play_arrow</span><span class="portal-icon-fallback">Play</span></button>';
            html += '<button type="button" class="btn btn-outline-danger btn-sm" title="Delete ' + soundEsc(name) + '" aria-label="Delete ' + soundEsc(name) + '" onclick="deleteSound(\'' + soundEsc(name) + '\')"><span class="portal-icon" aria-hidden="true">delete</span><span class="portal-icon-fallback">Delete</span></button>';
            html += '</div>';
        });
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
    try {
        const payload = {
            swipe_left: actionEditorBuild('swipe-left'),
            swipe_right: actionEditorBuild('swipe-right'),
            swipe_up: actionEditorBuild('swipe-up'),
            swipe_down: actionEditorBuild('swipe-down')
        };
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
        if (actionEditorHandleError(err)) return;
        console.error('Error saving swipe actions:', err);
        showMessage('Error saving swipe actions: ' + err.message, 'error');
    }
}

// ============================================================================
// Boot Actions
// ============================================================================

const BOOT_ACTION_PREFIXES = actionEditorSlotPrefixes('boot-action-');

async function bootActionsInitEditors() {
    await getDeviceInfo();
    actionEditorListRender('boot-action-editors', BOOT_ACTION_PREFIXES);
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

var bootLogoDraftSlot = 255;
var bootLogoSelectionLoaded = false;

function bootAppearanceColorValue(value, defaultValue) {
    return Number(value) === 4294967295
        ? '#' + Number(defaultValue).toString(16).padStart(6, '0')
        : '#' + Number(value).toString(16).padStart(6, '0');
}

function setBootAppearanceColorDefault(color, useDefault) {
    color.dataset.useDeviceDefault = useDefault ? 'true' : 'false';
}

function resetBootAppearanceColor(colorId) {
    const color = document.getElementById(colorId);
    if (!color) return;
    color.value = color.dataset.deviceDefaultColor;
    color.dispatchEvent(new Event('input', { bubbles: true }));
    setBootAppearanceColorDefault(color, true);
}

function showBootLogoPreview(slot) {
    const wrapper = document.getElementById('boot-logo-preview-wrap');
    const image = document.getElementById('boot-logo-preview');
    if (!wrapper || !image) return;
    if (slot === 255) {
        wrapper.hidden = true;
        image.removeAttribute('src');
        return;
    }
    image.src = '/api/boot-logo/thumbnail?slot=' + slot + '&t=' + Date.now();
    wrapper.hidden = false;
}

async function bootAppearanceInit() {
    const appearance = document.getElementById('boot-appearance-settings');
    if (!appearance) return;
    bootLogoSelectionLoaded = false;
    const saveButton = document.getElementById('system-screens-save');
    if (saveButton) saveButton.disabled = true;
    const status = document.getElementById('boot-logo-status');
    try {
        const response = await fetch('/api/config');
        if (!response.ok) throw new Error('Could not load system-screen settings');
        const config = await response.json();
        const caps = config.caps || {};
        appearance.hidden = !caps.boot_appearance;
        const logoSettings = document.getElementById('boot-logo-settings');
        if (logoSettings) logoSettings.hidden = !caps.boot_logo;
        if (!caps.boot_appearance) return;

        document.getElementById('boot-appearance-font-size').value =
            String(config.boot_appearance_font_size || 0);
        const textDefault = Number(config.boot_appearance_text_color) === 4294967295;
        const backgroundDefault = Number(config.boot_appearance_background_color) === 4294967295;
        const textColor = document.getElementById('boot-appearance-text-color');
        const backgroundColor = document.getElementById('boot-appearance-background-color');
        textColor.dataset.deviceDefaultColor = bootAppearanceColorValue(
            4294967295, config.boot_appearance_default_text_color);
        backgroundColor.dataset.deviceDefaultColor = bootAppearanceColorValue(
            4294967295, config.boot_appearance_default_background_color);
        textColor.value = bootAppearanceColorValue(
            config.boot_appearance_text_color, config.boot_appearance_default_text_color);
        backgroundColor.value = bootAppearanceColorValue(
            config.boot_appearance_background_color, config.boot_appearance_default_background_color);
        setBootAppearanceColorDefault(textColor, textDefault);
        setBootAppearanceColorDefault(backgroundColor, backgroundDefault);
        padInitBindableColor(document.getElementById('boot-appearance-text-color-wrap'));
        padInitBindableColor(document.getElementById('boot-appearance-background-color-wrap'));
        textColor.addEventListener('input', function() { setBootAppearanceColorDefault(textColor, false); });
        backgroundColor.addEventListener('input', function() { setBootAppearanceColorDefault(backgroundColor, false); });

        if (caps.boot_logo) {
            const logoResponse = await fetch('/api/boot-logo');
            if (!logoResponse.ok) throw new Error('Could not load boot-logo status');
            const logo = await logoResponse.json();
            bootLogoDraftSlot = Number(logo.active_slot);
            if (![0, 1, 255].includes(bootLogoDraftSlot)) {
                throw new Error('Boot-logo status is invalid');
            }
            if (bootLogoDraftSlot !== 255 && !logo['slot' + bootLogoDraftSlot + '_available']) {
                bootLogoDraftSlot = 255;
                if (status) status.textContent = 'The selected logo file is missing. Save Appearance to use the device logo.';
            }
            bootLogoSelectionLoaded = true;
            showBootLogoPreview(bootLogoDraftSlot);
        }
        if (saveButton) saveButton.disabled = false;
    } catch (err) {
        if (status) status.textContent = err.message;
        showMessage('Could not load system-screen settings: ' + err.message, 'error');
        console.error('Failed to load boot appearance:', err);
    }
}

async function uploadBootLogo() {
    const fileInput = document.getElementById('boot-logo-file');
    const status = document.getElementById('boot-logo-status');
    const file = fileInput && fileInput.files && fileInput.files[0];
    if (!file) {
        if (status) status.textContent = 'Choose a PNG or JPEG image first.';
        return;
    }
    if (file.size > 1024 * 1024) {
        if (status) status.textContent = 'Image exceeds the 1 MiB upload limit.';
        return;
    }
    if (!/^image\/(png|jpeg)$/.test(file.type)) {
        if (status) status.textContent = 'Choose a PNG or baseline (non-progressive) JPEG image.';
        return;
    }
    if (status) status.textContent = 'Uploading and validating image…';
    try {
        const response = await fetch('/api/boot-logo/upload', {
            method: 'POST',
            headers: { 'Content-Type': 'application/octet-stream' },
            body: file
        });
        const result = await response.json().catch(function() { return {}; });
        if (!response.ok || result.success !== true || ![0, 1].includes(Number(result.slot))) {
            throw new Error(result.message || 'Upload or validation failed');
        }
        bootLogoDraftSlot = Number(result.slot);
        bootLogoSelectionLoaded = true;
        showBootLogoPreview(bootLogoDraftSlot);
        fileInput.value = '';
        if (status) status.textContent = 'Uploaded and validated. Save Appearance to select this logo.';
    } catch (err) {
        if (status) status.textContent = err.message;
    }
}

async function deleteBootLogo() {
    const status = document.getElementById('boot-logo-status');
    const button = document.getElementById('boot-logo-delete-btn');
    if (!confirm('Delete all uploaded logos and use the device logo? This cannot be undone.')) return;
    if (button) button.disabled = true;
    if (status) status.textContent = 'Selecting the device logo…';
    let defaultLogoSaved = false;
    try {
        const configResponse = await fetch('/api/config?no_reboot=1', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ boot_logo_slot: 255 })
        });
        if (!configResponse.ok) {
            const result = await configResponse.json().catch(function() { return {}; });
            throw new Error(result.message || 'Could not select the device logo');
        }
        defaultLogoSaved = true;

        bootLogoDraftSlot = 255;
        bootLogoSelectionLoaded = true;
        showBootLogoPreview(255);
        const fileInput = document.getElementById('boot-logo-file');
        if (fileInput) fileInput.value = '';

        const response = await fetch('/api/boot-logo', { method: 'DELETE' });
        const result = await response.json().catch(function() { return {}; });
        if (!response.ok || result.success !== true) {
            throw new Error(result.message || 'Could not delete uploaded logos');
        }
        if (status) status.textContent = 'Uploaded logos deleted. The device logo will appear on the next boot.';
    } catch (err) {
        if (status) status.textContent = defaultLogoSaved
            ? err.message + ' The device logo is selected, but some uploaded files may remain.'
            : err.message;
        console.error('Failed to delete boot logo:', err);
    } finally {
        if (button) button.disabled = false;
    }
}

async function saveBootActions() {
    let actions;
    try {
        actions = actionEditorListBuild(BOOT_ACTION_PREFIXES);
    } catch (err) {
        if (actionEditorHandleError(err)) return;
        showMessage('Could not read boot actions: ' + err.message, 'error');
        return;
    }

    try {
        const response = await fetch('/api/component/boot-actions/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ actions: actions })
        });
        if (!response.ok) {
            const result = await response.json();
            throw new Error(result.message || 'Boot actions could not be saved');
        }
        showMessage('Boot actions saved. Changes take effect on the next boot.', 'success');
    } catch (err) {
        showMessage('Could not save boot actions: ' + err.message, 'error');
    }
}

async function saveSystemScreenAppearance() {
    const appearance = document.getElementById('boot-appearance-settings');
    const saveButton = document.getElementById('system-screens-save');
    if (!appearance || appearance.hidden || !saveButton || saveButton.disabled) {
        showMessage('System-screen settings are not ready. Reload this section before saving.', 'error');
        return;
    }
    const logoSettings = document.getElementById('boot-logo-settings');
    if (logoSettings && !logoSettings.hidden && !bootLogoSelectionLoaded) {
        showMessage('Could not confirm the selected logo. Reload this section before saving.', 'error');
        return;
    }
    saveButton.disabled = true;
    try {
        const textColor = document.getElementById('boot-appearance-text-color');
        const backgroundColor = document.getElementById('boot-appearance-background-color');
        for (const color of [textColor, backgroundColor]) {
            if (!/^#[0-9a-f]{6}$/i.test(color.value)) {
                throw new Error('Choose a valid six-digit color');
            }
        }
        const payload = {
            boot_appearance_font_size: Number(document.getElementById('boot-appearance-font-size').value),
            boot_appearance_text_color: textColor.dataset.useDeviceDefault === 'true'
                ? 4294967295 : parseInt(textColor.value.slice(1), 16),
            boot_appearance_background_color: backgroundColor.dataset.useDeviceDefault === 'true'
                ? 4294967295 : parseInt(backgroundColor.value.slice(1), 16)
        };
        if (logoSettings && !logoSettings.hidden) payload.boot_logo_slot = bootLogoDraftSlot;
        const response = await fetch('/api/config?no_reboot=1', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });
        if (!response.ok) {
            const result = await response.json();
            throw new Error(result.message || 'Configuration save failed');
        }
        showMessage('Appearance saved. Changes take effect on the next boot.', 'success');
        if (typeof setPendingReboot === 'function') setPendingReboot();
    } catch (err) {
        showMessage('Could not save appearance: ' + err.message, 'error');
    } finally {
        saveButton.disabled = false;
    }
}

// ============================================================================
// Hardware Button Actions (uses shared portal_action_editor.js)
// ============================================================================

// Flat list of every action editor prefix currently rendered, used to wire
// screen + sound dropdowns in one pass after the editors are built.
var HW_BUTTON_PREFIXES = [];

function hwButtonTapPrefixes(n) {
    return actionEditorSlotPrefixes('hwbtn-' + n + '-tap-');
}
function hwButtonHoldPrefixes(n) {
    return actionEditorSlotPrefixes('hwbtn-' + n + '-hold-');
}

async function initHwButtons() {
    var container = document.getElementById('hw-button-editors');
    if (!container) return;
    HW_BUTTON_PREFIXES = [];
    // The action-type picker renders from the firmware catalog cached on
    // deviceInfoCache; wait for it before building any action editor markup.
    await getDeviceInfo();
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
        html += '<div class="action-group-heading">Tap actions</div>';
        html += '<div id="hwbtn-' + n + '-tap-editors"></div>';
        html += '<div class="action-group-heading">Hold actions</div>';
        html += '<div id="hwbtn-' + n + '-hold-editors"></div>';
        html += '</div></details>';
    });
    container.innerHTML = html;

    buttons.forEach(function (btn, idx) {
        var n = idx + 1;
        var tapPrefixes = hwButtonTapPrefixes(n);
        var holdPrefixes = hwButtonHoldPrefixes(n);
        actionEditorListRender('hwbtn-' + n + '-tap-editors', tapPrefixes);
        actionEditorListRender('hwbtn-' + n + '-hold-editors', holdPrefixes);
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
    try {
        var buttons = [];
        for (var n = 1; n <= groupCount; n++) {
            buttons.push({
                tap_actions: actionEditorListBuild(hwButtonTapPrefixes(n)),
                hold_actions: actionEditorListBuild(hwButtonHoldPrefixes(n))
            });
        }
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
        if (actionEditorHandleError(err)) return;
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
    return actionEditorSlotPrefixes('mqtttrig-' + n + '-');
}

async function initMqttTriggers() {
    var container = document.getElementById('mqtt-trigger-editors');
    if (!container) return;
    MQTT_TRIGGER_PREFIXES = [];
    // The action-type picker renders from the firmware catalog cached on
    // deviceInfoCache; wait for it before building any action editor markup.
    await getDeviceInfo();
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
        html += '<div class="action-group-heading">Actions</div>';
        html += '<div id="mqtttrig-' + n + '-editors"></div>';
        html += '</div></details>';
    }
    container.innerHTML = html;

    for (var n = 1; n <= max; n++) {
        var prefixes = mqttTriggerPrefixes(n);
        actionEditorListRender('mqtttrig-' + n + '-editors', prefixes);
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
    try {
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
        if (actionEditorHandleError(err)) return;
        console.error('Error saving MQTT triggers:', err);
        showMessage('Error saving MQTT triggers: ' + err.message, 'error');
    }
}

// ============================================================================
// Timer Config (device-level, uses shared portal_action_editor.js)
// ============================================================================

const TIMER_IDS = [1, 2, 3];
function timerExpirePrefixes(tid) {
    return actionEditorSlotPrefixes('timer-' + tid + '-expire-');
}
const TIMER_EXPIRE_PREFIXES = TIMER_IDS.reduce(function(all, tid) {
    return all.concat(timerExpirePrefixes(tid));
}, []);

async function timerConfigInitEditors() {
    var container = document.getElementById('timer-config-editors');
    if (!container) return;
    // The action-type picker renders from the firmware catalog cached on
    // deviceInfoCache; wait for it before building any action editor markup.
    await getDeviceInfo();
    var html = '';
    TIMER_IDS.forEach(function(tid) {
        html += '<details class="editor-group" id="timer-' + tid + '-group">';
        html += '<summary>Timer ' + tid + '</summary>';
        html += '<div class="editor-group-body">';
        html += '<div id="timer-' + tid + '-expire-section">';
        html += '<div class="action-group-heading">On expire actions</div>';
        html += '<small style="display:block; margin-bottom:12px;">Actions are copied when a countdown starts and run once when it reaches zero. Stop and start a running timer after saving changes.</small>';
        html += '<div id="timer-' + tid + '-expire-editors"></div>';
        html += '</div>';
        html += '</div></details>';
    });
    container.innerHTML = html;
    TIMER_IDS.forEach(function(tid) {
        actionEditorListRender('timer-' + tid + '-expire-editors', timerExpirePrefixes(tid));
    });
}

async function loadTimerConfig() {
    try {
        const response = await fetch('/api/component/timers/config');
        if (!response.ok) return;
        const data = await response.json();
        TIMER_IDS.forEach(function(tid) {
            var tcfg = data[String(tid)] || {};
            actionEditorListLoad(timerExpirePrefixes(tid), tcfg.expire_actions || []);
        });
    } catch (err) {
        console.error('Failed to load timer config:', err);
    }
}

async function saveTimerConfig() {
    try {
        var payload = {};
        TIMER_IDS.forEach(function(tid) {
            payload[String(tid)] = { expire_actions: actionEditorListBuild(timerExpirePrefixes(tid)) };
        });
        const response = await fetch('/api/component/timers/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });
        if (response.ok) {
            showMessage('Timer config saved. Stop and start running countdowns to use the new actions.', 'success');
        } else {
            showMessage('Failed to save timer config', 'error');
        }
    } catch (err) {
        if (actionEditorHandleError(err)) return;
        console.error('Error saving timer config:', err);
        showMessage('Error saving timer config: ' + err.message, 'error');
    }
}
