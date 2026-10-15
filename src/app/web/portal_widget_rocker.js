padRegisterWidgetEditor({
    type: 'rocker',
    sections: [{ id: 'pad-edit-rocker-section', tab: 'Content' }],
    fields: [
        { id: 'pad-edit-rocker-axis', key: 'widget_rocker_axis', kind: 'text', default: 'vertical' },
        { id: 'pad-edit-rocker-color', key: 'widget_rocker_color', kind: 'color', default: () => PAD_FIRMWARE_DEFAULTS.widget_indicator_color },
        { id: 'pad-edit-rocker-opacity', key: 'widget_rocker_opacity', kind: 'number', default: 80 }
    ],
    actions: {
        tap: true,
        longPress: true,
        labels() {
            return document.getElementById('pad-edit-rocker-axis').value === 'horizontal'
                ? ['Left action', 'Right action'] : ['Up action', 'Down action'];
        }
    }
});