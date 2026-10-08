const ACTION_DELAY_MAX_DURATION_MS = 55000;

function actionEditorPausableActionLimit() {
    var delayAction = actionEditorCatalogEntry('delay');
    return delayAction && Number.isInteger(delayAction.max_pending_actions)
        ? delayAction.max_pending_actions : 1;
}

_actionEditorExtensions.push({
    type: 'delay',
    groups: function(prefix, opts) {
        var h = '';
        var pausableActionLimit = actionEditorPausableActionLimit();
        // Delay
        h += '<div id="' + prefix + '-delay-group" style="display:none;">';
        h += '<div class="form-group">';
        h += '<label class="form-label" for="' + prefix + '-delay-duration">Duration (ms)</label>';
        h += '<input type="number" class="form-control form-control-sm" id="' + prefix + '-delay-duration" min="1" max="' + ACTION_DELAY_MAX_DURATION_MS + '" value="1000" required>';
        h += '<small>Pauses this action list before running the following action. Up to ' + pausableActionLimit + ' pausable actions can be pending device-wide at a time; another pausable action stops its action list when all slots are occupied.</small>';
        h += '</div></div>';
        return h;
    },
    typeChanged: function(prefix, type) {
        var group = document.getElementById(prefix + '-delay-group');
        if (group) group.style.display = type === 'delay' ? '' : 'none';
    },
    load: function(prefix, action) {
        var el;
        el = document.getElementById(prefix + '-delay-duration');
        if (el) el.value = (action.type === 'delay' && action.duration_ms > 0) ? action.duration_ms : '1000';
    },
    build: function(prefix, type) {
        if (type !== 'delay') return null;
        var act = {};
        var delayDuration = document.getElementById(prefix + '-delay-duration');
        var delayValue = delayDuration ? Number(delayDuration.value) : 0;
        if (!Number.isInteger(delayValue) || delayValue < 1 || delayValue > ACTION_DELAY_MAX_DURATION_MS) {
            if (delayDuration) {
            delayDuration.setCustomValidity('Enter a whole number from 1 to ' + ACTION_DELAY_MAX_DURATION_MS + '.');
                delayDuration.reportValidity();
                delayDuration.focus();
            }
            throw new Error('Delay duration must be 1-' + ACTION_DELAY_MAX_DURATION_MS + ' milliseconds');
        }
        if (delayDuration) delayDuration.setCustomValidity('');
        act.duration_ms = delayValue;
        return act;
    }
});
