// ============================================================================
// Home Assistant Service Action Editor Extension
// ============================================================================
// Extends portal_action_editor.js with a "Home Assistant Service" action that
// calls the HA REST API (POST /api/services/<domain>/<service>). The domain is
// derived from the entity_id at dispatch time, so only entity_id, service, and
// optional data_json are edited here. Self-registers on load.

(function () {
    if (typeof _actionEditorExtensions === 'undefined') return;

    _actionEditorExtensions.push({
        // Form group HTML shown when ha_service is selected.
        groups: function (prefix) {
            var h = '';
            h += '<div id="' + prefix + '-ha_service-group" style="display:none;">';
            h += '<div class="form-group">';
            h += '<label class="form-label" for="' + prefix + '-ha-entity">Entity ID</label>';
            h += '<input type="text" class="form-control form-control-sm" id="' + prefix + '-ha-entity" maxlength="47" placeholder="light.living_room">';
            h += '</div>';
            h += '<div class="form-group">';
            h += '<label class="form-label" for="' + prefix + '-ha-service">Service</label>';
            h += '<input type="text" class="form-control form-control-sm" id="' + prefix + '-ha-service" maxlength="63" placeholder="toggle">';
            h += '</div>';
            h += '<div class="form-group">';
            h += '<label class="form-label" for="' + prefix + '-ha-data">Data (optional)</label>';
            h += '<input type="text" class="form-control form-control-sm" id="' + prefix + '-ha-data" maxlength="63" placeholder=\'{"brightness_pct":80}\'>';
            h += '<small>Optional JSON object to merge into the service call body. Configure the HA URL and token in <b>Home &rarr; Home Assistant</b>.</small>';
            h += '</div></div>';
            return h;
        },

        // Show/hide the ha_service group on type change.
        typeChanged: function (prefix, type) {
            var grp = document.getElementById(prefix + '-ha_service-group');
            if (grp) grp.style.display = (type === 'ha_service') ? '' : 'none';
        },

        // Populate fields from a saved action.
        load: function (prefix, action) {
            if (action.type !== 'ha_service') return;
            var e = document.getElementById(prefix + '-ha-entity');
            var s = document.getElementById(prefix + '-ha-service');
            var d = document.getElementById(prefix + '-ha-data');
            if (e) e.value = action.entity_id || '';
            if (s) s.value = action.service || '';
            if (d) d.value = action.data_json || '';
        },

        validate: function (action, prefix) {
            if (action.type !== 'ha_service' || !action.data_json) return;
            try {
                var data = JSON.parse(action.data_json);
                if (!data || typeof data !== 'object' || Array.isArray(data)) throw new Error();
            } catch (_) {
                throw Object.assign(new Error('data_json: Home Assistant data must be a JSON object after substitution'), { fieldIds: [prefix + '-ha-data'] });
            }
        },

        // Read fields into the action object.
        build: function (prefix, type) {
            if (type !== 'ha_service') return null;
            var e = document.getElementById(prefix + '-ha-entity');
            var s = document.getElementById(prefix + '-ha-service');
            var d = document.getElementById(prefix + '-ha-data');
            var act = {};
            if (e && e.value.trim()) act.entity_id = e.value.trim();
            if (s && s.value.trim()) act.service = s.value.trim();
            if (d && d.value.trim()) act.data_json = d.value.trim();
            return act;
        }
    });
})();
