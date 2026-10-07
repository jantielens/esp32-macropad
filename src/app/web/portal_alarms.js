var ALARM_RING_PREFIXES = actionEditorSlotPrefixes('alarm-ring-');
var ALARM_STOP_PREFIXES = actionEditorSlotPrefixes('alarm-stop-');

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
    var element = document.getElementById('alarm-state');
    if (!element) return;
    var response = await fetch('/api/component/alarms/status');
    if (!response.ok) throw new Error('Alarm status unavailable');
    var status = await response.json();
    if (document.getElementById('alarm-state') !== element) return;
    element.textContent = status.state === 'ringing' ? 'Ringing' : status.state === 'snoozed' ? 'Snoozed' : 'Idle';
    document.getElementById('alarm-readiness').textContent = status.ota_deferred ? 'Firmware update' : status.ready ? '' : 'Waiting for time synchronization';
    document.getElementById('alarm-snooze').disabled = status.state !== 'ringing';
    document.getElementById('alarm-cancel').disabled = status.state === 'idle';
    var warning = document.getElementById('alarm-warning');
    warning.hidden = !status.storage_error && !status.hook_error;
    warning.textContent = status.storage_error ? 'Alarm storage unavailable' : status.hook_error ? 'An alarm action failed' : '';
}

window.init_alarms_fragment = async function() {
    var form = document.getElementById('alarm-config-form');
    if (!form) return;
    try {
        await getDeviceInfo();
        actionEditorListRender('alarm-ring-editors', ALARM_RING_PREFIXES, null, {actionOptions:{alarmHook:true}});
        actionEditorListRender('alarm-stop-editors', ALARM_STOP_PREFIXES, null, {actionOptions:{alarmHook:true}});
        var responses = await Promise.all([fetch('/api/component/alarms/config'), fetch('/api/config')]);
        if (!responses[0].ok || !responses[1].ok) throw new Error('Alarm configuration unavailable');
        var config = (await responses[0].json())['1'];
        var device = await responses[1].json();
        if (document.getElementById('alarm-config-form') !== form) return;
        document.getElementById('alarm-enabled').checked = config.enabled;
        document.getElementById('alarm-time').value = String(config.hour).padStart(2, '0') + ':' + String(config.minute).padStart(2, '0');
        document.getElementById('alarm-timezone').textContent = device.timezone || 'UTC0';
        document.getElementById('alarm-snooze-minutes').value = config.snooze_minutes;
        document.getElementById('alarm-dismiss-minutes').value = config.auto_dismiss_minutes;
        for (var day = 0; day < 7; day++) document.getElementById('alarm-day-' + day).checked = !!(config.weekdays & (1 << day));
        actionEditorListLoad(ALARM_RING_PREFIXES, config.on_ring || []);
        actionEditorListLoad(ALARM_STOP_PREFIXES, config.on_stop || []);
        actionEditorWireFragment(ALARM_RING_PREFIXES.concat(ALARM_STOP_PREFIXES));
        form.addEventListener('submit', async function(event) {
            event.preventDefault();
            var button = document.getElementById('alarm-save');
            button.disabled = true;
            try {
                var response = await fetch('/api/component/alarms/config', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(alarmConfigBuild())});
                if (!response.ok) throw new Error('Alarm configuration was not saved');
                showMessage('Alarm saved', 'success');
                await alarmStatusRefresh();
            } catch (error) { showMessage(error.message, 'error'); }
            finally { button.disabled = false; }
        });
        ['snooze', 'cancel'].forEach(function(command) {
            document.getElementById('alarm-' + command).addEventListener('click', async function() {
                try {
                    var response = await fetch('/api/component/alarms/' + command, {method:'POST'});
                    if (!response.ok) throw new Error('Alarm command failed');
                    await alarmStatusRefresh();
                } catch (error) { showMessage(error.message, 'error'); }
            });
        });
        async function poll() {
            if (document.getElementById('alarm-config-form') !== form) return;
            try { await alarmStatusRefresh(); } catch (error) {
                if (document.getElementById('alarm-config-form') === form) document.getElementById('alarm-readiness').textContent = 'Offline';
            }
            if (document.getElementById('alarm-config-form') === form) setTimeout(poll, 2000);
        }
        await poll();
    } catch (error) { showMessage(error.message, 'error'); }
};