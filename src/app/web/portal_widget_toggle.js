function padToggleNormalize(value) {
    return String(value).replace(/^[ \t\r\n\f\v]+|[ \t\r\n\f\v]+$/g, '').replace(/[A-Z]/g, value => value.toLowerCase());
}

padRegisterWidgetEditor({
    type: 'toggle',
    sections: [{ id: 'pad-edit-toggle-section', tab: 'Content' }],
    fields: [
        { id: 'pad-edit-toggle-source', key: 'widget_data_binding', kind: 'text', default: '', binding: { isWidgetBinding: true } },
        { id: 'pad-edit-toggle-on-value', key: 'widget_toggle_on_value', kind: 'text', default: 'on' },
        { id: 'pad-edit-toggle-off-value', key: 'widget_toggle_off_value', kind: 'text', default: 'off' },
        { id: 'pad-edit-toggle-read-only', key: 'widget_toggle_read_only', kind: 'checkbox', default: false },
        { id: 'pad-edit-toggle-on-color', key: 'widget_toggle_on_color', kind: 'color', default: () => PAD_FIRMWARE_DEFAULTS.widget_series_color },
        { id: 'pad-edit-toggle-off-color', key: 'widget_toggle_off_color', kind: 'color', default: () => PAD_FIRMWARE_DEFAULTS.widget_track_color },
        { id: 'pad-edit-toggle-knob-color', key: 'widget_toggle_knob_color', kind: 'color', default: () => PAD_FIRMWARE_DEFAULTS.widget_indicator_color },
        { id: 'pad-edit-toggle-width-pct', key: 'widget_toggle_width_pct', kind: 'number', default: 100 },
        { id: 'pad-edit-toggle-icon-position', key: 'widget_toggle_icon_position', kind: 'text', default: 'left' }
    ],
    actions: { tap: true, longPress: true, labels: () => ['Turn-on actions', 'Turn-off actions'] },
    icon: { fixedPosition: true, width: 0.4, height: 0.4 },
    build(button) {
        const fail = (message, fieldIds) => { throw Object.assign(new Error(message), { fieldIds }); };
        const on = padToggleNormalize(button.widget_toggle_on_value || '');
        const off = padToggleNormalize(button.widget_toggle_off_value || '');
        if (!on || !off || on === off || new TextEncoder().encode(on).length > 63 || new TextEncoder().encode(off).length > 63)
            fail('On and off values must be distinct, nonempty, and at most 63 bytes', ['pad-edit-toggle-on-value', 'pad-edit-toggle-off-value']);
        for (const name of ['on', 'off', 'knob']) {
            if (!/^#[0-9a-f]{6}$/i.test(button['widget_toggle_' + name + '_color'] || ''))
                fail('Choose a #RRGGBB color', ['pad-edit-toggle-' + name + '-color']);
        }
        if (!Number.isInteger(button.widget_toggle_width_pct) || button.widget_toggle_width_pct < 20 || button.widget_toggle_width_pct > 100)
            fail('Switch size must be an integer from 20 to 100', ['pad-edit-toggle-width-pct']);
        if (!['left', 'above', 'below'].includes(button.widget_toggle_icon_position))
            fail('Choose left, above, or below for the icon', ['pad-edit-toggle-icon-position']);
    }
});