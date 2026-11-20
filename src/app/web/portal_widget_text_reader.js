padRegisterWidgetEditor({
    type: 'text_reader',
    available: info => !!info?.widget_catalog?.some(widget => widget.type === 'text_reader'),
    sections: [{ id: 'pad-edit-text-reader-section', tab: 'Content' }],
    actions: { tap: false, longPress: false },
    fields: [
        { id: 'pad-edit-text-reader-source', key: 'widget_data_binding', kind: 'text', default: '', binding: {} },
        { id: 'pad-edit-text-reader-style', key: 'widget_text_reader_style', kind: 'text', default: '', binding: {} },
        { id: 'pad-edit-text-reader-viewport', key: 'widget_text_reader_viewport', kind: 'text', default: '' },
        { id: 'pad-edit-text-reader-navigation', key: 'widget_text_reader_show_navigation', kind: 'checkbox', default: true },
        { id: 'pad-edit-text-reader-indicator-track', key: 'widget_text_reader_indicator_track_color', kind: 'color', default: '', binding: {} },
        { id: 'pad-edit-text-reader-indicator-color', key: 'widget_text_reader_indicator_color', kind: 'color', default: '', binding: {} }
    ],
    init() {
        this.fields.filter(field => field.kind === 'color').forEach(field => {
            const input = document.getElementById(field.id);
            padInitBindableColor(input.closest('.bindable-color'));
            const label = document.querySelector('label[for="' + field.id + '"]');
            const reset = document.createElement('button');
            reset.type = 'button';
            reset.className = 'reset-hint material-symbols-outlined list-color-reset';
            reset.title = 'Reset to default';
            reset.setAttribute('aria-label', 'Reset position indicator ' + label.textContent.replace(/\s*fx\s*$/, '').trim() + ' to default');
            reset.textContent = 'restart_alt';
            reset.addEventListener('click', () => padSetBindableColor(field.id, ''));
            label.appendChild(reset);
            const sync = () => {
                const key = field.key.endsWith('_track_color') ? 'bg_color' : 'fg_color';
                const value = padGetEffectiveDefault(key);
                input.dataset.colorFallback = padColorNormalizeLiteral(value) || padColorValue(value);
                padRefreshBindableColor(input);
                reset.style.display = input.value.trim() ? 'inline' : 'none';
            };
            input.addEventListener('input', sync);
            sync();
        });
    }
});
