padRegisterWidgetEditor({
    type: 'numericrocker',
    sections: [
        { id: 'pad-edit-numericrocker-section', tab: 'Content' },
        { id: 'pad-edit-numericrocker-adjust-section', tab: 'Actions' }
    ],
    fields: [
        { id: 'pad-edit-numericrocker-axis', key: 'widget_numericrocker_axis', kind: 'text', default: 'horizontal' },
        { id: 'pad-edit-numericrocker-small-step', key: 'widget_numericrocker_small_step', kind: 'number', default: 1 },
        { id: 'pad-edit-numericrocker-large-step', key: 'widget_numericrocker_large_step', kind: 'number', default: 10 },
        { id: 'pad-edit-numericrocker-zone-scale', key: 'widget_numericrocker_zone_scale_pct', kind: 'number', default: 100 },
        { id: 'pad-edit-numericrocker-color', key: 'widget_numericrocker_color', kind: 'color', default: () => PAD_FIRMWARE_DEFAULTS.widget_indicator_color },
        { id: 'pad-edit-numericrocker-opacity', key: 'widget_numericrocker_opacity', kind: 'number', default: 80 }
    ],
    actions: { tap: true, longPress: false, prefixes: ['pad-edit-nr-adjust'] },
    init() {
        document.getElementById('pad-edit-nr-adjust-container').innerHTML = actionEditorHTML(
            'pad-edit-nr-adjust', 'Adjustment Action', { showBleHint: true, showKeyHelp: true });
    },
    load(button) {
        actionEditorLoad('pad-edit-nr-adjust', button.widget_numericrocker_action || null);
    },
    build(button) {
        const action = actionEditorBuild('pad-edit-nr-adjust');
        if (action.type) button.widget_numericrocker_action = action;
    }
});