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
        actionEditorListRender(g.prefix + '-editors', ssaGroupPrefixes(g));
    });
}

async function loadShutterSessionActions() {
    try {
        const response = await fetch('/api/component/shutter-session-actions/config');
        if (!response.ok) return;
        const data = await response.json();
        SSA_GROUPS.forEach(function (g) {
            actionEditorListLoad(ssaGroupPrefixes(g), data[g.field] || []);
        });
    } catch (err) {
        console.error('Failed to load shutter session actions:', err);
    }
}

async function saveShutterSessionActions() {
    var payload = {};
    SSA_GROUPS.forEach(function (g) {
        payload[g.field] = actionEditorListBuild(ssaGroupPrefixes(g));
    });
    try {
        const response = await fetch('/api/component/shutter-session-actions/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });
        if (response.ok) {
            showMessage('Shutter session actions saved', 'success');
        } else {
            showMessage('Failed to save shutter session actions', 'error');
        }
    } catch (err) {
        console.error('Error saving shutter session actions:', err);
        showMessage('Error saving shutter session actions: ' + err.message, 'error');
    }
}

window.init_shutter_session_actions_fragment = async function () {
    if (typeof shutterSessionActionsInitEditors === 'function') await shutterSessionActionsInitEditors();
    if (typeof loadShutterSessionActions === 'function') loadShutterSessionActions();
    if (typeof actionEditorWireFragment === 'function') actionEditorWireFragment(ssaAllPrefixes());
};
