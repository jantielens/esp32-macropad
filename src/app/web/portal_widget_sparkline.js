(() => {
    const maxWindow = 7 * 86400;
    const maxPoints = 1024;
    const fields = [];
    const input = name => document.getElementById('pad-edit-sparkline-' + name);
    function field(name, key, kind, defaultValue, binding) {
        fields.push({ id: 'pad-edit-sparkline-' + name, key, kind, default: defaultValue, binding });
    }
    function fail(name, message) {
        throw Object.assign(new Error(message), { fieldIds: ['pad-edit-sparkline-' + name] });
    }
    function duration(name, units) {
        const value = Number(input(name).value);
        const unit = Number(input(name + '-unit').value);
        padWidgetValidateField(input(name), { kind: 'number' });
        if (!input(name).value.trim() || !Number.isFinite(value) || !units.includes(unit)) fail(name, 'Enter a valid duration');
        return value * unit;
    }
    function sampling() {
        const range = duration('window', [1, 60, 3600, 86400]);
        if (range < 10 || range > maxWindow) fail('window', 'Time range must be between 10 seconds and 7 days');
        const interval = duration('interval', [1, 60, 3600]);
        if (interval < 0.1) fail('interval', 'Requested interval must be at least 100 milliseconds');
        const windowSeconds = Math.round(range);
        const slots = Math.max(2, Math.min(maxPoints, Math.floor(windowSeconds / 0.1), Math.ceil(windowSeconds / interval)));
        return { windowSeconds, slots, interval: windowSeconds / slots };
    }
    function setDuration(name, seconds, units) {
        const unit = units.find(candidate => seconds >= candidate && seconds % candidate === 0) || 1;
        input(name).value = String(seconds / unit);
        input(name + '-unit').value = String(unit);
    }
    for (let source = 1; source <= 3; source++) {
        const suffix = source === 1 ? '' : '-' + source;
        const keySuffix = source === 1 ? '' : '_' + source;
        field('data-binding' + suffix, 'widget_data_binding' + keySuffix, 'text', '', { isWidgetBinding: true });
        field('line-color' + suffix, 'widget_sparkline_line_color' + keySuffix, 'color',
            () => PAD_FIRMWARE_DEFAULTS['widget_series_color' + keySuffix], {});
        field('current-label' + suffix, 'widget_sparkline_current_label' + keySuffix, 'text', '', {});
        field('ref-' + source + '-y', 'widget_sparkline_ref_' + source + '_y', 'number', '');
        field('ref-' + source + '-color', 'widget_sparkline_ref_' + source + '_color', 'color',
            () => PAD_FIRMWARE_DEFAULTS.widget_reference_color, {});
        field('ref-' + source + '-pattern', 'widget_sparkline_ref_' + source + '_pattern', 'number', 0);
    }
    for (const name of ['min', 'max']) {
        field(name, 'widget_sparkline_' + name, 'bindable-number', '', { isWidgetBinding: true });
        field(name + '-fmt', 'widget_sparkline_' + name + '_fmt', 'text', '');
        field(name + '-label-color', 'widget_sparkline_' + name + '_label_color', 'color', '', {});
    }
    for (const [name, value] of [['line-width', 2], ['line-offset', 0], ['smooth', 0],
        ['marker-size-min', 0], ['marker-size-max', 0], ['current-dot', 0], ['label-width', 0]]) {
        field(name, 'widget_sparkline_' + name.replaceAll('-', '_'), 'number', value);
    }
    field('unified-scale', 'widget_sparkline_unified_scale', 'checkbox', true);
    field('ref-in-view', 'widget_sparkline_ref_in_view', 'checkbox', false);
    padRegisterWidgetEditor({
        type: 'sparkline', fields,
        sections: [{ id: 'pad-edit-sparkline-section', tab: 'Content' }],
        actions: { tap: true, longPress: true },
        icon: { fixedPosition: true, width: 1, height: 1 },
        load(button) {
            const range = button.widget_sparkline_window === undefined ? 300 : button.widget_sparkline_window;
            const slots = button.widget_sparkline_slots === undefined ? 60 : button.widget_sparkline_slots;
            setDuration('window', range, [86400, 3600, 60, 1]);
            setDuration('interval', range / slots, [3600, 60, 1]);
            for (const suffix of ['', '-2', '-3']) {
                const keySuffix = suffix.replace('-', '_');
                input('ha-entity' + suffix).value = button['widget_sparkline_ha_entity' + keySuffix] || '';
                input('ha-stat' + suffix).value = button['widget_sparkline_ha_stat' + keySuffix] || 'mean';
            }
        },
        build(button) {
            const sample = sampling();
            button.widget_sparkline_window = sample.windowSeconds;
            button.widget_sparkline_slots = sample.slots;
            if ((window.__device_caps || {}).ha_history !== true) return;
            for (const suffix of ['', '-2', '-3']) {
                const entityInput = input('ha-entity' + suffix);
                padWidgetValidateField(entityInput, { kind: 'text' });
                const entity = entityInput.value.trim();
                if (entity && !input('data-binding' + suffix).value.trim()) fail('ha-entity' + suffix, 'History entity requires a live data binding');
                if (!entity) continue;
                const stat = input('ha-stat' + suffix).value;
                if (!['mean', 'state', 'sum'].includes(stat)) fail('ha-stat' + suffix, 'Select a history statistic');
                const keySuffix = suffix.replace('-', '_');
                button['widget_sparkline_ha_entity' + keySuffix] = entity;
                button['widget_sparkline_ha_stat' + keySuffix] = stat;
            }
        },
        refresh() {
            const historyAvailable = (window.__device_caps || {}).ha_history === true;
            document.querySelectorAll('.sparkline-ha-source').forEach(element => { element.style.display = historyAvailable ? '' : 'none'; });
            for (const suffix of ['', '-2', '-3']) {
                input('ha-fields' + suffix).style.display = '';
                input('ha-entity' + suffix).disabled = false;
                input('ha-stat' + suffix).disabled = false;
                input('ha-dependency' + suffix).textContent = '';
                const stat = input('ha-stat' + suffix).value;
                input('ha-stat-help' + suffix).textContent = stat === 'state' ? 'Most recently recorded value.' :
                    stat === 'sum' ? 'Accumulated total for total sensors.' : 'Average value during each statistics period.';
            }
            const unit = Number(input('window-unit').value);
            input('window').min = String(10 / unit);
            input('window').max = String(maxWindow / unit);
            input('interval').min = String(0.1 / Number(input('interval-unit').value));
            input('window-warn').style.display = 'none';
            input('ha-warn').style.display = 'none';
            input('points-warn').style.display = 'none';
            try {
                const sample = sampling();
                input('slots').value = sample.slots;
                input('resolution').textContent = sample.slots + ' data points, actual interval ' + Number(sample.interval.toFixed(3)) + ' seconds.';
                if (Math.ceil(sample.windowSeconds / duration('interval', [1, 60, 3600])) > maxPoints) {
                    input('points-warn').textContent = 'Capacity is limited to 1024 data points; actual interval is shown above.';
                    input('points-warn').style.display = '';
                }
                const history = historyAvailable && ['', '-2', '-3'].some(suffix => input('ha-entity' + suffix).value.trim());
                if (history && sample.interval < (sample.windowSeconds > 86400 ? 3600 : 300)) {
                    input('ha-warn').textContent = sample.windowSeconds > 86400 ?
                        'Hourly historical values are visually interpolated; live data uses the finer interval.' :
                        'Five-minute historical values are held between periods; live data uses the finer interval.';
                    input('ha-warn').style.display = '';
                }
            } catch (error) {
                input('resolution').textContent = error.message;
            }
        }
    });
})();

function padUpdateSparklineEditor() {
    padWidgetEditor('sparkline').refresh();
}