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
var SSA_SLOTS = 3;

function ssaSlotPrefix(groupPrefix, i) { return groupPrefix + '-' + i; }

function ssaAllPrefixes() {
    var out = [];
    SSA_GROUPS.forEach(function (g) {
        for (var i = 1; i <= SSA_SLOTS; i++) out.push(ssaSlotPrefix(g.prefix, i));
    });
    return out;
}

function shutterSessionActionsInitEditors() {
    var container = document.getElementById('ssa-action-editors');
    if (!container) return;
    var html = '';
    SSA_GROUPS.forEach(function (g) {
        html += '<details class="editor-group" id="' + g.prefix + '-group" open>';
        html += '<summary>' + g.label + '</summary>';
        html += '<div class="editor-group-body">';
        for (var i = 1; i <= SSA_SLOTS; i++) {
            html += '<div style="margin-bottom:8px;"><strong style="font-size:12px;color:#86868b;">Action ' + i + '</strong></div>';
            html += actionEditorHTML(ssaSlotPrefix(g.prefix, i));
        }
        html += '</div></details>';
    });
    container.innerHTML = html;
}

async function loadShutterSessionActions() {
    try {
        const response = await fetch('/api/component/shutter-session-actions/config');
        if (!response.ok) return;
        const data = await response.json();
        SSA_GROUPS.forEach(function (g) {
            var arr = data[g.field] || [];
            for (var i = 1; i <= SSA_SLOTS; i++) {
                actionEditorLoad(ssaSlotPrefix(g.prefix, i), arr[i - 1] || {});
            }
        });
    } catch (err) {
        console.error('Failed to load shutter session actions:', err);
    }
}

async function saveShutterSessionActions() {
    var payload = {};
    SSA_GROUPS.forEach(function (g) {
        var arr = [];
        for (var i = 1; i <= SSA_SLOTS; i++) {
            var act = actionEditorBuild(ssaSlotPrefix(g.prefix, i));
            if (act && act.type) arr.push(act);
        }
        payload[g.field] = arr;
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

window.init_shutter_session_actions_fragment = function () {
    if (typeof shutterSessionActionsInitEditors === 'function') shutterSessionActionsInitEditors();
    if (typeof loadShutterSessionActions === 'function') loadShutterSessionActions();
    if (typeof actionEditorWireFragment === 'function') actionEditorWireFragment(ssaAllPrefixes());
};
