padRegisterWidgetEditor({
    type: 'epub_reader',
    available: info => !!info?.widget_catalog?.some(widget => widget.type === 'epub_reader'),
    sections: [{ id: 'pad-edit-epub-reader-section', tab: 'Content' }],
    actions: { tap: false, longPress: false },
    fields: [
        { id: 'pad-edit-epub-reader-path', key: 'widget_epub_path', kind: 'text', default: '' }
    ]
});
