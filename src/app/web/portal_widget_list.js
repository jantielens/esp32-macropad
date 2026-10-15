padRegisterWidgetEditor({
    type: 'list',
    sections: [{ id: 'pad-edit-list-section', tab: 'Content' }],
    fields: [
        { id: 'pad-edit-list-provider-id', key: 'widget_data_binding', kind: 'text', default: '' },
        { id: 'pad-edit-list-filter', key: 'widget_data_binding_2', kind: 'text', default: '' }
    ],
    actions: { tap: true, longPress: true, labels: () => ['Select action', 'Long-press action'] }
});