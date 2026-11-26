padRegisterWidgetEditor({
    type: 'ereader',
    available: info => !!info?.widget_catalog?.some(widget => widget.type === 'ereader'),
    sections: [{ id: 'pad-edit-ereader-section', tab: 'Content' }],
    actions: { tap: false, longPress: false },
    fields: [
        { id: 'pad-edit-ereader-style', key: 'widget_ereader_style', kind: 'text', default: '' }
    ]
});
