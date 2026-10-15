padRegisterWidgetEditor({
    type: 'camera_preview',
    optionId: 'pad-edit-camera-preview-widget-option',
    available: info => !!(info && info.has_camera === true),
    sections: [{ id: 'pad-edit-camera-preview-section', tab: 'Content' }],
    fields: [{ id: 'pad-edit-camera-preview-scale', key: 'widget_camera_scale', kind: 'text', default: 'cover' }],
    actions: { tap: true, longPress: true }
});