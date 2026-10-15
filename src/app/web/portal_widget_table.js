padRegisterWidgetEditor({
    type: 'table',
    sections: [{ id: 'pad-edit-table-section', tab: 'Content' }],
    actions: { tap: true, longPress: true },
    fields: [
        { id: 'pad-edit-table-data-binding', key: 'widget_data_binding', default: '', binding: { isWidgetBinding: true } },
        { id: 'pad-edit-table-style', key: 'widget_table_style', default: '' },
        { id: 'pad-edit-table-scrollable', key: 'widget_table_scrollable', kind: 'boolean-select', default: true }
    ]
});