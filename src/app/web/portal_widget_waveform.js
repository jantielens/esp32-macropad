padRegisterWidgetEditor({
    type: 'waveform',
    available: info => !!(info && info.widget_catalog && info.widget_catalog.some(widget => widget.type === 'waveform')),
    sections: [{ id: 'pad-edit-waveform-section', tab: 'Content' }],
    actions: { tap: true, longPress: true },
    fields: [
        ...['#4CAF50', '#2196F3', '#9C27B0', '#F44336'].map((color, index) => ({
            id: 'pad-edit-waveform-color-' + (index + 1), key: 'widget_waveform_color_' + (index + 1), kind: 'color', default: color, binding: {}
        })),
        { id: 'pad-edit-waveform-threshold-color', key: 'widget_waveform_threshold_color', kind: 'color', default: '#FF9800', binding: {} },
        { id: 'pad-edit-waveform-trigger-color', key: 'widget_waveform_trigger_color', kind: 'color', default: '#FFFFFF', binding: {} },
        { id: 'pad-edit-waveform-line-width', key: 'widget_waveform_line_width', kind: 'number', default: 2 },
        { id: 'pad-edit-waveform-sensor', key: 'widget_waveform_sensor', kind: 'number', default: 0 },
        { id: 'pad-edit-waveform-show-threshold', key: 'widget_waveform_show_threshold', kind: 'checkbox', default: true },
        { id: 'pad-edit-waveform-show-trigger', key: 'widget_waveform_show_trigger', kind: 'checkbox', default: true },
        { id: 'pad-edit-waveform-show-target', key: 'widget_waveform_show_target', kind: 'checkbox', default: true },
        { id: 'pad-edit-waveform-invert-y', key: 'widget_waveform_invert_y', kind: 'checkbox', default: false }
    ]
});