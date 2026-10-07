var ALARM_RING_PREFIXES = actionEditorSlotPrefixes('alarm-ring-');
var ALARM_STOP_PREFIXES = actionEditorSlotPrefixes('alarm-stop-');

function alarmScheduleSummary() {
    var form = document.getElementById('alarm-config-form');
    var days = [];
    var names = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
    [1, 2, 3, 4, 5, 6, 0].forEach(function(day) {
        if (document.getElementById('alarm-day-' + day).checked) days.push(names[day]);
    });
    var enabled = document.getElementById('alarm-enabled').checked;
    var time = document.getElementById('alarm-time').value;
    var summary = days.length ? 'Weekly: ' + days.join(', ') + ' at ' + time + '.'
        : 'Only once: next valid ' + time + ', then automatically disabled. Snooze can still ring again within that session.';
    document.getElementById('alarm-repeat-summary').textContent = enabled ? summary : 'Disabled. Enable and save to schedule. ' + summary;
    var target = document.getElementById('alarm-once-target');
    target.hidden = !enabled || days.length > 0;
    target.textContent = form.dataset.onceLocal && form.dataset.dirty !== 'true'
        ? 'Scheduled: ' + form.dataset.onceLocal + ' (device timezone).'
        : 'Saving enabled one-shot settings schedules the next occurrence, usually today or tomorrow. Unchanged settings keep the saved occurrence.';
}

function alarmConfigBuild() {
    var time = document.getElementById('alarm-time').value.split(':');
    var weekdays = 0;
    for (var day = 0; day < 7; day++) if (document.getElementById('alarm-day-' + day).checked) weekdays |= 1 << day;
    return {'1': {
        enabled: document.getElementById('alarm-enabled').checked,
        hour: Number(time[0]), minute: Number(time[1]), weekdays: weekdays,
        snooze_minutes: Number(document.getElementById('alarm-snooze-minutes').value),
        auto_dismiss_minutes: Number(document.getElementById('alarm-dismiss-minutes').value),
        on_ring: actionEditorListBuild(ALARM_RING_PREFIXES),
        on_stop: actionEditorListBuild(ALARM_STOP_PREFIXES)
    }};
}

async function alarmStatusRefresh() {
    var element = document.getElementById('alarm-readiness');
    if (!element) return;
    var response = await fetch('/api/component/alarms/status');
    if (!response.ok) throw new Error('Alarm status unavailable');
    var status = await response.json();
    if (document.getElementById('alarm-readiness') !== element) return;
    element.textContent = status.ota_deferred ? 'Firmware update' : status.ready ? '' : 'Waiting for time synchronization';
    var warning = document.getElementById('alarm-warning');
    warning.hidden = !status.storage_error && !status.hook_error;
    warning.textContent = status.storage_error ? 'Alarm storage unavailable' : status.hook_error ? 'An alarm action failed' : '';
    var form = document.getElementById('alarm-config-form');
    if (form.dataset.dirty !== 'true') {
        document.getElementById('alarm-enabled').checked = status.enabled;
        form.dataset.onceLocal = status.once_local || '';
    }
    alarmScheduleSummary();
}

window.init_alarms_fragment = async function() {
    var form = document.getElementById('alarm-config-form');
    if (!form) return;
    try {
        await getDeviceInfo();
        actionEditorListRender('alarm-ring-editors', ALARM_RING_PREFIXES, null, {actionOptions:{alarmHook:true}});
        actionEditorListRender('alarm-stop-editors', ALARM_STOP_PREFIXES, null, {actionOptions:{alarmHook:true}});
        var response = await fetch('/api/component/alarms/config');
        if (!response.ok) throw new Error('Alarm configuration unavailable');
        var config = (await response.json())['1'];
        if (document.getElementById('alarm-config-form') !== form) return;
        document.getElementById('alarm-enabled').checked = config.enabled;
        document.getElementById('alarm-time').value = String(config.hour).padStart(2, '0') + ':' + String(config.minute).padStart(2, '0');
        document.getElementById('alarm-snooze-minutes').value = config.snooze_minutes;
        document.getElementById('alarm-dismiss-minutes').value = config.auto_dismiss_minutes;
        for (var day = 0; day < 7; day++) document.getElementById('alarm-day-' + day).checked = !!(config.weekdays & (1 << day));
        actionEditorListLoad(ALARM_RING_PREFIXES, config.on_ring || []);
        actionEditorListLoad(ALARM_STOP_PREFIXES, config.on_stop || []);
        actionEditorWireFragment(ALARM_RING_PREFIXES.concat(ALARM_STOP_PREFIXES));
        form.dataset.dirty = 'false';
        function settingsChanged() {
            form.dataset.dirty = 'true';
            alarmScheduleSummary();
        }
        form.addEventListener('input', settingsChanged);
        form.addEventListener('change', settingsChanged);
        alarmScheduleSummary();
        form.addEventListener('submit', async function(event) {
            event.preventDefault();
            var button = document.getElementById('alarm-save');
            button.disabled = true;
            try {
                var submitted = JSON.stringify(alarmConfigBuild());
                var response = await fetch('/api/component/alarms/config', {method:'POST', headers:{'Content-Type':'application/json'}, body:submitted});
                if (!response.ok) throw new Error('Alarm configuration was not saved');
                if (document.getElementById('alarm-config-form') !== form) return;
                form.dataset.dirty = JSON.stringify(alarmConfigBuild()) === submitted ? 'false' : 'true';
                showMessage('Alarm saved', 'success');
                await alarmStatusRefresh();
            } catch (error) { showMessage(error.message, 'error'); }
            finally { button.disabled = false; }
        });
        await alarmStatusRefresh();
    } catch (error) { showMessage(error.message, 'error'); }
};