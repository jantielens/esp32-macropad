(() => {
    const prefix = 'pad-edit-list-';
    const sources = () => deviceInfoCache?.list_sources || [];
    function initialId() {
        const used = new Set([...padState.mainButtons, ...padState.overlayButtons, ...padState.templateButtons]
            .map(button => button.widget_list_id).filter(Boolean));
        let index = 1;
        while (used.has('list_' + index)) index++;
        return 'list_' + index;
    }
    function showFields() {
        const source = sources().find(item => item.token === document.getElementById(prefix + 'source').value);
        document.getElementById(prefix + 'fields').textContent = source
            ? 'Item fields: ' + source.fields.map(field => field.name).join(', ')
            : 'Select a source to see its item fields.';
    }
    function showStyle(slot, visible) {
        const wrap = document.getElementById(prefix + slot + '-style-wrap');
        const button = document.getElementById(prefix + slot + '-style-toggle');
        wrap.style.display = visible ? 'flex' : 'none';
        button.classList.toggle('active', visible);
        button.setAttribute('aria-expanded', String(visible));
    }
    function syncStyle(slot) {
        showStyle(slot, !!document.getElementById(prefix + slot + '-style').value.trim());
    }
    function labelColor(slot, foreground) {
        let literal = '', binding = '';
        const style = document.getElementById(prefix + slot + '-style').value;
        bindingSplitParams(style).forEach(part => {
            const colon = part.indexOf(':');
            if (colon < 0 || part.slice(0, colon).replace(/^ +/, '') !== 'color') return;
            const value = part.slice(colon + 1);
            const normalized = padColorNormalizeLiteral(value);
            if (normalized) literal = normalized;
            else if (value.includes('[')) binding = value;
        });
        return binding || literal || foreground;
    }
    function syncColors() {
        const colorValue = value => padColorNormalizeLiteral(value) || padColorValue(value);
        const background = colorValue(padGetEffectiveDefault('bg_color'));
        const border = colorValue(padGetEffectiveDefault('border_color'));
        const foreground = colorValue(padGetEffectiveDefault('fg_color'));
        const width = String(padGetEffectiveDefault('border_width')).trim();
        const numericWidth = width !== '' && Number.isFinite(Number(width));
        const rowBackground = padGetBindableColor(prefix + 'background') || background;
        const labelForeground = padGetBindableColor('pad-edit-fg-color') || foreground;
        const fallbacks = {
            'background': background,
            'border-color': border,
            'selected-background': colorValue(rowBackground),
            'selected-border-color': numericWidth ? (Math.trunc(Number(width)) > 0 ? border : foreground) : '',
            'selected-top-color': colorValue(labelColor('top', labelForeground)),
            'selected-bottom-color': colorValue(labelColor('bottom', labelForeground)),
            'indicator-track': background,
            'indicator-color': foreground
        };
        Object.entries(fallbacks).forEach(([name, fallback]) => {
            const input = document.getElementById(prefix + name);
            input.dataset.colorFallback = fallback;
            padRefreshBindableColor(input);
            const swatch = input.closest('.bindable-color').querySelector('.bc-swatch');
            const dynamicBorder = name === 'selected-border-color' && !numericWidth && !input.value.trim();
            if (dynamicBorder) swatch.classList.add('bc-binding');
            swatch.title = dynamicBorder ? 'Automatic color depends on the button default border width' : '';
            document.getElementById(input.id + '-reset').style.display = input.value.trim() ? 'inline' : 'none';
        });
    }
    const color = (name, key) => ({ id: prefix + name, key, kind: 'color', default: '', binding: {} });
    padRegisterWidgetEditor({
        type: 'list',
        available: info => !!info?.widget_catalog?.some(widget => widget.type === 'list') &&
            Array.isArray(info.list_sources),
        sections: [
            { id: 'pad-edit-list-section', tab: 'Content' },
            { id: 'pad-edit-list-action-note', tab: 'Actions' }
        ],
        fields: [
            { id: prefix + 'id', key: 'widget_list_id', kind: 'text', default: initialId },
            { id: prefix + 'source', key: 'widget_data_binding', kind: 'text', default: () => sources()[0]?.token || '', preview: showFields },
            { id: prefix + 'top', key: 'widget_list_label_top', kind: 'text', default: '[item:title|Untitled]', binding: {} },
            { id: prefix + 'bottom', key: 'widget_list_label_bottom', kind: 'text', default: '', binding: {} },
            { id: prefix + 'top-style', key: 'widget_list_style_top', kind: 'text', default: '', binding: {}, preview: () => syncStyle('top') },
            { id: prefix + 'bottom-style', key: 'widget_list_style_bottom', kind: 'text', default: '', binding: {}, preview: () => syncStyle('bottom') },
            color('background', 'widget_list_bg_color'),
            color('border-color', 'widget_list_border_color'),
            { id: prefix + 'border-width', key: 'widget_list_border_width', kind: 'number', default: '' },
            { id: prefix + 'allow-selection', key: 'widget_list_allow_selection', kind: 'checkbox', default: true },
            color('selected-background', 'widget_list_selected_bg_color'),
            color('selected-border-color', 'widget_list_selected_border_color'),
            { id: prefix + 'selected-border-width', key: 'widget_list_selected_border_width', kind: 'number', default: '' },
            color('selected-top-color', 'widget_list_selected_top_color'),
            color('selected-bottom-color', 'widget_list_selected_bottom_color'),
            { id: prefix + 'visible-items', key: 'widget_list_visible_items', kind: 'number', default: 0 },
            { id: prefix + 'padding', key: 'widget_list_padding', kind: 'number', default: 6 },
            { id: prefix + 'spacing', key: 'widget_list_spacing', kind: 'number', default: 4 },
            { id: prefix + 'radius', key: 'widget_list_radius', kind: 'number', default: 8 },
            { id: prefix + 'viewport', key: 'widget_list_viewport', kind: 'text', default: '' },
            color('indicator-track', 'widget_list_indicator_track_color'),
            color('indicator-color', 'widget_list_indicator_color')
        ],
        actions: { tap: true, longPress: true, labels: () => ['Item tap actions', 'Item long-press actions'] },
        load(config) {
            const select = document.getElementById(prefix + 'source');
            select.querySelectorAll('[data-unavailable]').forEach(option => option.remove());
            if (config.widget_data_binding && !sources().some(source => source.token === config.widget_data_binding)) {
                const option = document.createElement('option');
                option.value = config.widget_data_binding;
                option.textContent = config.widget_data_binding + ' (unavailable)';
                option.dataset.unavailable = 'true';
                select.appendChild(option);
            }
        },
        init() {
            const select = document.getElementById(prefix + 'source');
            select.replaceChildren();
            sources().forEach(source => {
                const option = document.createElement('option');
                option.value = source.token;
                option.textContent = source.title;
                select.appendChild(option);
            });
            select.addEventListener('change', showFields);
            for (const slot of ['top', 'bottom']) {
                document.getElementById(prefix + slot + '-style').addEventListener('input', syncColors);
                document.getElementById(prefix + slot + '-style-toggle').addEventListener('click', () => {
                    const wrap = document.getElementById(prefix + slot + '-style-wrap');
                    const visible = wrap.style.display === 'none';
                    showStyle(slot, visible);
                    if (visible) document.getElementById(prefix + slot + '-style').focus();
                });
            }
            this.fields.filter(field => field.kind === 'color').forEach(field => {
                const input = document.getElementById(field.id);
                padInitBindableColor(input.closest('.bindable-color'));
                const label = document.querySelector('label[for="' + field.id + '"]');
                const reset = document.createElement('button');
                reset.type = 'button';
                reset.id = field.id + '-reset';
                reset.className = 'reset-hint material-symbols-outlined list-color-reset';
                reset.title = 'Reset to default';
                const group = input.closest('details').querySelector('summary').textContent.trim();
                reset.setAttribute('aria-label', 'Reset ' + group + ' ' + label.textContent.replace(/\s*fx\s*$/, '').trim() + ' to default');
                reset.textContent = 'restart_alt';
                reset.addEventListener('click', () => padSetBindableColor(field.id, ''));
                label.appendChild(reset);
                input.addEventListener('input', syncColors);
            });
            document.getElementById('pad-edit-fg-color').addEventListener('input', syncColors);
            syncColors();
        }
    });
})();
