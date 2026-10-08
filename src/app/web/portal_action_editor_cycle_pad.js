function actionEditorNormalizeCyclePadExclusions(value) {
    var configuredMax = (typeof deviceInfoCache !== 'undefined' && deviceInfoCache)
        ? Number(deviceInfoCache.max_pads) : 0;
    var maxPads = configuredMax > 0 ? Math.floor(configuredMax) : 16;
    var unique = {};
    String(value || '').split(',').forEach(function(token) {
        token = token.trim();
        if (!/^[0-9]+$/.test(token)) return;
        var pad = Number(token);
        if (pad >= 1 && pad <= maxPads) unique[pad] = true;
    });
    return Object.keys(unique).map(Number).sort(function(first, second) { return first - second; }).join(',');
}

_actionEditorExtensions.push({
    type: 'cycle_pad',
    groups: function(prefix) {
        var html = '<div id="' + prefix + '-cycle-pad-group" style="display:none;">';
        html += '<div class="form-group">';
        html += '<label class="form-label" for="' + prefix + '-cycle-pad-direction">Direction</label>';
        html += '<select class="form-select form-select-sm" id="' + prefix + '-cycle-pad-direction">';
        html += '<option value="next">Next</option><option value="previous">Previous</option>';
        html += '</select></div>';
        html += '<div class="form-group">';
        html += '<label><input type="checkbox" id="' + prefix + '-cycle-pad-wrap" checked> Wrap at first or last pad</label>';
        html += '</div>';
        html += '<div class="form-group">';
        html += '<label class="form-label" for="' + prefix + '-cycle-pad-exclusions">Excluded Pads</label>';
        html += '<input type="text" class="form-control form-control-sm" id="' + prefix + '-cycle-pad-exclusions" placeholder="e.g. 2, 5, 8">';
        html += '<small>Optional comma-separated 1-based pad numbers.</small>';
        return html + '</div></div>';
    },
    typeChanged: function(prefix, type) {
        var group = document.getElementById(prefix + '-cycle-pad-group');
        if (group) group.style.display = type === 'cycle_pad' ? '' : 'none';
    },
    load: function(prefix, action) {
        var input = document.getElementById(prefix + '-cycle-pad-direction');
        if (input) input.value = action.direction === 'previous' ? 'previous' : 'next';
        input = document.getElementById(prefix + '-cycle-pad-wrap');
        if (input) input.checked = action.wrap !== false;
        input = document.getElementById(prefix + '-cycle-pad-exclusions');
        if (input) input.value = actionEditorNormalizeCyclePadExclusions(action.excluded_pads || '');
    },
    build: function(prefix, type) {
        if (type !== 'cycle_pad') return null;
        var direction = document.getElementById(prefix + '-cycle-pad-direction');
        var wrap = document.getElementById(prefix + '-cycle-pad-wrap');
        var exclusions = document.getElementById(prefix + '-cycle-pad-exclusions');
        var action = { direction: direction && direction.value === 'previous' ? 'previous' : 'next', wrap: wrap ? wrap.checked : true };
        var normalized = actionEditorNormalizeCyclePadExclusions(exclusions ? exclusions.value : '');
        if (exclusions) exclusions.value = normalized;
        if (normalized) action.excluded_pads = normalized;
        return action;
    }
});