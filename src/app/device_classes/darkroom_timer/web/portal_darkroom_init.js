// portal_darkroom_init.js — Fragment init + relay-config helpers for the
// darkroom-timer device class.
//
// Self-contained: the relay-slot editor helpers (RELAY_SLOT_PREFIXES,
// relayConfigInitEditors, loadRelayConfig, saveRelayConfig) live here rather
// than in the shared portal_config_actions.js so the shared file carries zero
// device-class code. They drive the shared action-editor primitives
// (actionEditorHTML / actionEditorLoad / actionEditorBuild) exposed by
// portal_action_editor.js, and talk to the /api/relay REST endpoint.
//
// Convention: window['init_' + itemId.replace(/-/g, '_') + '_fragment']()
// is called by portal_nav.js after loading the corresponding fragment HTML.

// ============================================================================
// Relay slot definitions
// ============================================================================

const RELAY_SLOT_PREFIXES = ['relay-enlarger-on', 'relay-enlarger-off', 'relay-safelight-on', 'relay-safelight-off'];
const RELAY_SLOT_KEYS = ['enlarger_on', 'enlarger_off', 'safelight_on', 'safelight_off'];
const RELAY_SLOT_LABELS = {
    'relay-enlarger-on':   'Enlarger ON',
    'relay-enlarger-off':  'Enlarger OFF',
    'relay-safelight-on':  'Safelight ON',
    'relay-safelight-off': 'Safelight OFF'
};
var relayConfigLoaded = false;

async function relayConfigInitEditors() {
    var container = document.getElementById('relay-config-editors');
    if (!container) return;
    // The action-type picker renders from the firmware catalog cached on
    // deviceInfoCache; wait for it before building any action editor markup.
    await getDeviceInfo();
    var html = '';
    RELAY_SLOT_PREFIXES.forEach(function(prefix) {
        html += '<details class="editor-group" id="' + prefix + '-group">';
        html += '<summary>' + RELAY_SLOT_LABELS[prefix] + '</summary>';
        html += '<div class="editor-group-body">';
        html += actionEditorHTML(prefix, '', { operations: true });
        html += '</div></details>';
    });
    container.innerHTML = html;
}

async function loadRelayConfig() {
    relayConfigLoaded = false;
    try {
        const response = await fetch('/api/relay');
        if (!response.ok) {
            const error = await response.json();
            throw new Error(error.error || 'Relay configuration is unavailable');
        }
        const data = await response.json();
        RELAY_SLOT_PREFIXES.forEach(function(prefix, i) {
            actionEditorLoad(prefix, data[RELAY_SLOT_KEYS[i]] || {});
        });
        relayConfigLoaded = true;
    } catch (err) {
        console.error('Failed to load relay config:', err);
        showMessage('Could not load relay configuration: ' + err.message, 'error');
    }
}

async function saveRelayConfig() {
    if (!relayConfigLoaded) {
        showMessage('Load relay configuration before saving.', 'error');
        return;
    }
    try {
        var payload = { action_format_version: 1 };
        RELAY_SLOT_PREFIXES.forEach(function(prefix, i) {
            payload[RELAY_SLOT_KEYS[i]] = actionEditorBuild(prefix);
        });
        const response = await fetch('/api/relay', {
            method: 'PUT',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });
        if (response.ok) {
            showMessage('Relay config saved', 'success');
        } else {
            const error = await response.json();
            showMessage(error.error || 'Failed to save relay config', 'error');
        }
    } catch (err) {
        if (actionEditorHandleError(err)) return;
        console.error('Error saving relay config:', err);
        showMessage('Error saving relay config: ' + err.message, 'error');
    }
}

// ============================================================================
// Darkroom / Relay Config fragment
// ============================================================================

window.init_darkroom_fragment = async function () {
    if (typeof relayConfigInitEditors === 'function') await relayConfigInitEditors();
    if (typeof loadRelayConfig === 'function') await loadRelayConfig();
    if (typeof actionEditorWireFragment === 'function') actionEditorWireFragment(RELAY_SLOT_PREFIXES);

    // Populate screen options for relay action slots. getDeviceInfo() is
    // cached (relayConfigInitEditors already awaited it above), so this
    // resolves immediately without a second network request.
    getDeviceInfo().then(function (version) {
        if (typeof actionEditorPopulateScreens === 'function' && typeof RELAY_SLOT_PREFIXES !== 'undefined') {
            actionEditorPopulateScreens(RELAY_SLOT_PREFIXES, (version && version.available_screens) || []);
        }
    });

    // Populate sound options for relay action slots
    fetch('/api/sounds/list').then(function (r) { return r.ok ? r.json() : []; })
        .then(function (sounds) {
            if (typeof actionEditorPopulateSounds === 'function' && typeof RELAY_SLOT_PREFIXES !== 'undefined') {
                actionEditorPopulateSounds(RELAY_SLOT_PREFIXES, sounds);
            }
        }).catch(function () {});

    // Wire save button
    var saveBtn = document.getElementById('darkroom-save-btn');
    if (saveBtn) saveBtn.addEventListener('click', function () {
        if (typeof saveRelayConfig === 'function') saveRelayConfig();
    });
};

// ============================================================================
// Prints fragment
// ============================================================================

window.init_prints_fragment = function () {
    if (typeof portalPrintsInit === 'function') portalPrintsInit();
};
