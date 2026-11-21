// portal_shutter_config.js — Sensor Configuration + Session Save Actions for
// the Shutter Tester device class. Bundled into the shutter_pages chunk and
// only loaded on IS_SHUTTER_TESTER builds. Defines:
//   - init_shutter_fragment() for the Sensor Configuration page
//   - init_shutter_session_actions_fragment() + SSA helpers for Session Save Actions
//   - registers shutter NVS keys with the shared saveFragmentConfig() loop

// ============================================================================
// Register shutter NVS config keys with the shared config save loop
// ============================================================================
if (typeof window.registerConfigFields === 'function') {
    window.registerConfigFields([
        'shutter_preset_id',
        'sensor_offset_x_mm',
        'sensor_offset_y_mm'
    ]);
}

// ============================================================================
// Sensor Configuration fragment
// ============================================================================
window.init_shutter_fragment = function () {
    initConfigFragment('shutter-save-btn', true);
};

// ============================================================================
// Session Save Actions — two events, each with up to 3 sequential actions
// ============================================================================
var SSA_GROUPS = [
    { field: 'save_start_actions',    label: 'On Save Started',   prefix: 'ssa-start' },
    { field: 'save_complete_actions', label: 'On Save Completed', prefix: 'ssa-complete' }
];
var ssaLoaded = false;

function ssaGroupPrefixes(g) {
    return actionEditorSlotPrefixes(g.prefix + '-');
}

function ssaAllPrefixes() {
    return SSA_GROUPS.reduce(function (all, g) { return all.concat(ssaGroupPrefixes(g)); }, []);
}

async function shutterSessionActionsInitEditors() {
    var container = document.getElementById('ssa-action-editors');
    if (!container) return;
    // The action-type picker renders from the firmware catalog cached on
    // deviceInfoCache; wait for it before building any action editor markup.
    await getDeviceInfo();
    var html = '';
    SSA_GROUPS.forEach(function (g) {
        html += '<details class="editor-group" id="' + g.prefix + '-group" open>';
        html += '<summary>' + g.label + '</summary>';
        html += '<div class="editor-group-body">';
        html += '<div id="' + g.prefix + '-editors"></div>';
        html += '</div></details>';
    });
    container.innerHTML = html;
    SSA_GROUPS.forEach(function (g) {
        actionEditorListRender(g.prefix + '-editors', ssaGroupPrefixes(g), null,
            { actionOptions: { operations: true } });
    });
}

async function loadShutterSessionActions() {
    ssaLoaded = false;
    try {
        const response = await fetch('/api/component/shutter-session-actions/config');
        if (!response.ok) {
            const error = await response.json();
            throw new Error(error.error || 'Shutter session actions are unavailable');
        }
        const data = await response.json();
        SSA_GROUPS.forEach(function (g) {
            actionEditorListLoad(ssaGroupPrefixes(g), data[g.field] || []);
        });
        ssaLoaded = true;
        actionConfigShowReset(data);
    } catch (err) {
        console.error('Failed to load shutter session actions:', err);
        showMessage('Could not load shutter session actions: ' + err.message, 'error');
    }
}

async function saveShutterSessionActions() {
    if (!ssaLoaded) {
        showMessage('Load shutter session actions before saving.', 'error');
        return;
    }
    try {
        var payload = { action_format_version: 1 };
        SSA_GROUPS.forEach(function (g) {
            payload[g.field] = actionEditorListBuild(ssaGroupPrefixes(g));
        });
        const response = await fetch('/api/component/shutter-session-actions/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });
        if (response.ok) {
            showMessage('Shutter session actions saved', 'success');
        } else {
            const error = await response.json();
            showMessage(error.error || 'Failed to save shutter session actions', 'error');
        }
    } catch (err) {
        if (actionEditorHandleError(err)) return;
        console.error('Error saving shutter session actions:', err);
        showMessage('Error saving shutter session actions: ' + err.message, 'error');
    }
}

window.init_shutter_session_actions_fragment = async function () {
    if (typeof shutterSessionActionsInitEditors === 'function') await shutterSessionActionsInitEditors();
    if (typeof loadShutterSessionActions === 'function') loadShutterSessionActions();
    if (typeof actionEditorWireFragment === 'function') actionEditorWireFragment(ssaAllPrefixes());
};
