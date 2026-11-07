(() => {
    const prefix = 'pad-edit-option-';
    const id = (slot, name) => prefix + slot + '-' + name;
    const key = (slot, name) => 'widget_option_' + name + (slot === 1 ? '' : '_' + slot);
    const input = (slot, name) => document.getElementById(id(slot, name));
    const field = (slot, name, kind, binding) => ({
        id: id(slot, name), key: key(slot, name), kind, default: '', ...(binding ? { binding: {} } : {})
    });
    const fields = [
        { id: prefix + 'mode', key: 'widget_option_mode', kind: 'text', default: 'none', preview: syncMode },
        { id: prefix + 'source', key: 'widget_data_binding', kind: 'text', default: '', binding: { isWidgetBinding: true }, preview: syncMode },
        { id: prefix + 'backgrounds', key: 'widget_option_backgrounds', kind: 'checkbox', default: false },
        { id: prefix + 'group-background', key: 'widget_option_group_background', kind: 'checkbox', default: false },
        { id: prefix + 'group-color', key: 'widget_option_group_color', kind: 'color', default: '', binding: {} },
        { id: prefix + 'background-color', key: 'widget_option_background_color', kind: 'color', default: '', binding: {} },
        { id: prefix + 'selected-fg', key: 'widget_option_selected_fg_color', kind: 'color', default: '' },
        { id: prefix + 'selected-bg', key: 'widget_option_selected_bg_color', kind: 'color', default: '' },
        { id: prefix + 'font-size', key: 'widget_option_font_size', kind: 'number', default: 0 },
        { id: prefix + 'icon-size', key: 'widget_option_icon_size_pct', kind: 'number', default: 0 },
        { id: prefix + 'padding', key: 'widget_option_icon_badge_padding', kind: 'number', default: 0 },
        { id: prefix + 'inset', key: 'widget_option_inset', kind: 'number', default: 4 },
    ];
    for (let slot = 1; slot <= 5; slot++) {
        fields.push(field(slot, 'label', 'label', true), field(slot, 'value', 'text', false),
            field(slot, 'fg_color', 'color', true), field(slot, 'bg_color', 'color', true),
            field(slot, 'state', 'text', true));
    }

    function readIcon(slot) {
        const value = input(slot, 'icon').value;
        return value ? JSON.parse(value) : null;
    }
    function setIcon(slot, icon) {
        input(slot, 'icon').value = icon ? JSON.stringify(icon) : '';
        input(slot, 'icon-name').textContent = icon ? (icon.catalog_id || 'Local image') : 'Choose icon';
        input(slot, 'icon-remove').disabled = !icon;
    }
    function fail(slot, name, message) {
        throw Object.assign(new Error('Option ' + slot + ': ' + message), { fieldIds: [id(slot, name)] });
    }
    function values() {
        return Array.from({ length: 5 }, (_, index) => index + 1)
            .filter(slot => input(slot, 'label').value.trim() || input(slot, 'icon').value)
            .map(slot => input(slot, 'value').value.trim());
    }
    function syncMode() {
        document.getElementById(prefix + 'source-group').hidden =
            document.getElementById(prefix + 'mode').value !== 'bound' &&
            !document.getElementById(prefix + 'source').value.trim();
    }
    function syncColors() {
        const track = PAD_FIRMWARE_DEFAULTS.widget_track_color || '';
        const background = padGetBindableColor(prefix + 'background-color') || track;
        const foreground = padGetBindableColor('pad-edit-fg-color') || padGetEffectiveDefault('fg_color');
        fields.filter(field => field.kind === 'color').forEach(field => {
            const colorInput = document.getElementById(field.id);
            let fallback = track;
            if (field.key === 'widget_option_selected_fg_color') fallback = PAD_FIRMWARE_DEFAULTS.widget_indicator_color;
            else if (field.key === 'widget_option_selected_bg_color') fallback = PAD_FIRMWARE_DEFAULTS.widget_series_color;
            else if (/^widget_option_fg_color(?:_\d+)?$/.test(field.key)) fallback = foreground;
            else if (/^widget_option_bg_color(?:_\d+)?$/.test(field.key)) fallback = background;
            colorInput.dataset.colorFallback = padColorValue(fallback);
            padRefreshBindableColor(colorInput);
            document.getElementById(field.id + '-reset').style.display = colorInput.value.trim() ? 'inline' : 'none';
        });
    }
    padRegisterWidgetEditor({
        type: 'option_selector',
        available: info => !!(info && info.has_display === true),
        sections: [{ id: 'pad-edit-option-selector-section', tab: 'Content' },
            { id: 'pad-edit-option-slots-section', tab: 'Content' },
            { id: 'pad-edit-option-colors-section', tab: 'Content' },
            { id: 'pad-edit-option-layout-section', tab: 'Content' },
            { id: 'pad-edit-option-action-note', tab: 'Actions' }],
        fields,
        disclosureSettings: Array.from({ length: 5 }, (_, index) => ({
            group: id(index + 1, 'section'), key: key(index + 1, 'icon')
        })),
        actions: {
            tap: true, longPress: false,
            labels: () => ['Option action', 'Long-press action'],
            buildTap: () => actionEditorListBuild(padActionPrefixes('tap'), '{value}', values())
        },
        init() {
            const container = document.getElementById('pad-edit-option-slots');
            container.innerHTML = Array.from({ length: 5 }, (_, index) => {
                const slot = index + 1;
                const text = (name, label, binding = false, help = '') => '<div class="form-group"><label for="' + id(slot, name) + '">' + label +
                    (binding ? ' <span class="fx-hint" onclick="showBindingHelp()">fx</span>' : '') + '</label>' +
                    '<input type="text" id="' + id(slot, name) + '" maxlength="' + (name === 'value' ? 63 : 191) + '"' +
                    (help ? ' aria-describedby="' + id(slot, name) + '-help"' : '') + '>' +
                    (help ? '<small id="' + id(slot, name) + '-help">' + help + '</small>' : '') + '</div>';
                const color = (name, label) => '<div class="form-group"><label for="' + id(slot, name) + '">' + label +
                    ' <span class="fx-hint" onclick="showBindingHelp()">fx</span></label><div class="bindable-color"><div class="bc-swatch"></div>' +
                    '<input type="text" class="bc-input" id="' + id(slot, name) + '" maxlength="191" placeholder="Auto"></div></div>';
                return '<details class="editor-group" id="' + id(slot, 'section') + '"><summary>Option ' + slot + '</summary>' +
                    '<div class="editor-group-body">' + text('label', 'Label', true) +
                    '<div class="form-group"><button type="button" class="btn btn-secondary" id="' + id(slot, 'choose') +
                    '"><span id="' + id(slot, 'icon-name') + '">Choose icon</span></button> ' +
                    '<button type="button" class="btn btn-outline-secondary" id="' + id(slot, 'icon-remove') + '">Remove icon</button>' +
                    '<input type="hidden" id="' + id(slot, 'icon') + '"><small>Choose a label or an icon.</small></div>' +
                    text('value', 'Value', false, 'Use {value} in an action to insert this value. Required in Bound mode; otherwise optional. If set, use 1–63 ASCII letters, digits, _ or -. Nonempty values must be distinct in Local and Bound modes.') +
                    text('state', 'State', true, 'Optional: enabled, disabled, hidden, or a binding.') +
                    '<div class="widget-row option-selector-row">' + color('fg_color', 'Foreground') +
                    color('bg_color', 'Background') + '</div>' +
                    '</div></details>';
            }).join('');
            document.getElementById(prefix + 'mode').addEventListener('change', syncMode);
            document.getElementById(prefix + 'source').addEventListener('change', syncMode);
            syncMode();
            for (let slot = 1; slot <= 5; slot++) {
                input(slot, 'choose').addEventListener('click', () => padOpenIconPicker({
                    currentIcon: readIcon(slot),
                    onSelect(icon) {
                        setIcon(slot, icon);
                        input(slot, 'label').value = '';
                    }
                }));
                input(slot, 'icon-remove').addEventListener('click', () => {
                    setIcon(slot, null);
                    padWorkspaceEdit();
                });
                input(slot, 'label').addEventListener('input', () => {
                    if (input(slot, 'label').value) setIcon(slot, null);
                });
            }
            fields.filter(field => field.kind === 'color').forEach(field => {
                const colorInput = document.getElementById(field.id);
                const container = colorInput.closest('.bindable-color');
                if (container.dataset.optionAutoInit) return;
                container.dataset.optionAutoInit = '1';
                const reset = document.createElement('button');
                reset.type = 'button';
                reset.id = field.id + '-reset';
                reset.className = 'reset-hint material-symbols-outlined option-color-reset';
                reset.title = 'Reset to default';
                const label = document.querySelector('label[for="' + field.id + '"]');
                const slot = field.id.match(/^pad-edit-option-(\d+)-/);
                const name = (slot ? 'Option ' + slot[1] + ' ' : '') +
                    label.textContent.replace(/\s*fx\s*$/, '').trim();
                reset.setAttribute('aria-label', 'Use automatic color for ' + name);
                reset.textContent = 'restart_alt';
                reset.addEventListener('click', () => padSetBindableColor(field.id, ''));
                label.appendChild(reset);
                colorInput.addEventListener('input', syncColors);
            });
            const foreground = document.getElementById('pad-edit-fg-color');
            if (!foreground.dataset.optionColorsInit) {
                foreground.dataset.optionColorsInit = '1';
                foreground.addEventListener('input', syncColors);
            }
            syncColors();
        },
        load(button) {
            for (let slot = 1; slot <= 5; slot++) setIcon(slot, button[key(slot, 'icon')] || null);
        },
        build(button) {
            const mode = button.widget_option_mode;
            const seen = new Set();
            for (const name of ['source', 'group-color', 'background-color']) {
                if (new TextEncoder().encode(document.getElementById(prefix + name).value).length > 191)
                    throw Object.assign(new Error('Use at most 191 UTF-8 bytes.'), { fieldIds: [prefix + name] });
            }
            for (const name of ['selected-fg', 'selected-bg']) {
                const value = document.getElementById(prefix + name).value.trim();
                if (value && !/^#[0-9a-f]{6}$/i.test(value))
                    throw Object.assign(new Error('Choose a #RRGGBB color.'), { fieldIds: [prefix + name] });
            }
            for (let slot = 1; slot <= 5; slot++) {
                const icon = readIcon(slot);
                const label = button[key(slot, 'label')] || '';
                const value = button[key(slot, 'value')] || '';
                if (icon) button[key(slot, 'icon')] = icon;
                if (value && (!(label || icon) || !/^[A-Za-z0-9_-]{1,63}$/.test(value)))
                    fail(slot, 'value', 'A value requires a label or icon and must use 1–63 ASCII letters, digits, _ or -.');
                if ((label || icon) && !value && mode === 'bound')
                    fail(slot, 'value', 'A value is required in Bound mode.');
                if (label && icon) fail(slot, 'label', 'Choose either a label or an icon.');
                const normalized = value.toLowerCase();
                if (value && mode !== 'none' && seen.has(normalized)) fail(slot, 'value', 'Values must be distinct.');
                if (value) seen.add(normalized);
                for (const name of ['label', 'fg_color', 'bg_color', 'state']) {
                    if (new TextEncoder().encode(button[key(slot, name)] || '').length > 191)
                        fail(slot, name, 'Use at most 191 UTF-8 bytes.');
                }
            }
        }
    });
})();
