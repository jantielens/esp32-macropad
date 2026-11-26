(() => {
    const prefix = 'pad-edit-progress-';
    const input = name => document.getElementById(prefix + name);
    const fail = (name, message) => {
        throw Object.assign(new Error(message), { fieldIds: [prefix + name] });
    };
    const field = (name, key, kind, value, binding) =>
        ({ id: prefix + name, key, kind, default: value, binding });
    const fields = [
        field('current', 'widget_data_binding', 'text', '', { isWidgetBinding: true }),
        field('max', 'widget_progress_max', 'text', '100', { isWidgetBinding: true }),
        field('units', 'widget_progress_units', 'text', '25', { isWidgetBinding: true }),
        field('min', 'widget_progress_min', 'text', '0', { isWidgetBinding: true }),
        field('invert', 'widget_progress_invert', 'checkbox', false),
        field('layout', 'widget_progress_layout', 'text', 'grid'),
        field('columns', 'widget_progress_columns', 'text', 'auto'),
        field('shape', 'widget_progress_shape', 'text', 'circle'),
        field('aspect', 'widget_progress_aspect', 'number', 1),
        field('thickness', 'widget_progress_thickness', 'text', 'auto'),
        field('gap-x', 'widget_progress_gap_x', 'text', 'auto'),
        field('gap-y', 'widget_progress_gap_y', 'text', 'auto'),
        field('rounding', 'widget_progress_rounding', 'number', 0),
        field('active-color', 'widget_progress_active_color', 'color', 'auto', {}),
        field('inactive-style', 'widget_progress_inactive_style', 'text', 'auto'),
        field('inactive-color', 'widget_progress_inactive_color', 'color', 'auto', {}),
        field('inactive-size', 'widget_progress_inactive_size', 'number', 25),
        field('partial', 'widget_progress_partial', 'text', 'size'),
        field('order', 'widget_progress_order', 'text', 'rows'),
        field('origin', 'widget_progress_origin', 'text', 'top_left'),
        field('seed', 'widget_progress_seed', 'number', 84721),
        field('direction', 'widget_progress_fill_direction', 'text', 'auto'),
        field('effect', 'widget_progress_effect', 'text', 'grow'),
        field('duration', 'widget_anim_ms', 'number', 300),
        field('stagger', 'widget_progress_stagger_ms', 'number', 0)
    ];
    const base = {
        layout: 'grid', columns: 'auto', shape: 'circle', aspect: 1, thickness: 'auto',
        'gap-x': 'auto', 'gap-y': 'auto', rounding: 0, 'active-color': 'auto',
        'inactive-style': 'auto', 'inactive-color': 'auto', 'inactive-size': 25,
        partial: 'size', order: 'rows', origin: 'top_left', seed: 84721,
        direction: 'auto', effect: 'grow', duration: 300, stagger: 0
    };
    const recipes = {
        growing_dots: { columns: 5, 'gap-x': 6, 'gap-y': 6 },
        uniform_dots: { columns: 6, 'gap-x': 8, 'gap-y': 8, 'inactive-size': 100, partial: 'fill' },
        dense_dots: { columns: 12, 'gap-x': 5, 'gap-y': 5, 'inactive-size': 100, partial: 'fill', origin: 'bottom_left', direction: 'up' },
        soft_waffle: { columns: 10, shape: 'block', 'gap-x': 2, 'gap-y': 2, rounding: 20, 'inactive-size': 100, partial: 'fill', origin: 'bottom_left', direction: 'up' },
        pixel_blocks: { columns: 12, shape: 'block', 'gap-x': 2, 'gap-y': 2, 'inactive-size': 100, partial: 'fill' },
        capsule_matrix: { columns: 6, shape: 'block', aspect: 22 / 12, 'gap-x': 5, 'gap-y': 7, rounding: 50, 'inactive-size': 100, partial: 'fill', origin: 'bottom_left', direction: 'up' },
        rounded_strip: { layout: 'horizontal', shape: 'block', thickness: 19, 'gap-x': 4, 'gap-y': 4, rounding: 50, 'inactive-size': 100, partial: 'fill' },
        fine_strip: { layout: 'horizontal', shape: 'block', thickness: 14, 'gap-x': 3, 'gap-y': 3, 'inactive-size': 100, partial: 'fill' },
        vertical_stack: { layout: 'vertical', shape: 'block', thickness: 34, 'gap-x': 6, 'gap-y': 4, rounding: 50, 'inactive-size': 100, partial: 'fill', origin: 'bottom_left', direction: 'up' }
    };
    function syncColors() {
        const foreground = padGetBindableColor('pad-edit-fg-color') || padGetEffectiveDefault('fg_color');
        const fallback = padColorNormalizeLiteral(foreground) || padColorValue(foreground);
        for (const name of ['active-color', 'inactive-color']) {
            const el = input(name);
            el.dataset.colorFallback = fallback;
            padRefreshBindableColor(el);
            const swatch = el.closest('.bindable-color').querySelector('.bc-swatch');
            swatch.title = el.value.trim() ? '' : (name === 'active-color'
                ? 'Automatic: button foreground'
                : 'Automatic: muted foreground or panel ink');
        }
    }
    function syncResets() {
        fields.filter(f => f.id !== prefix + 'seed').forEach(f => {
            const el = document.getElementById(f.id);
            const reset = document.getElementById(f.id + '-reset');
            if (!reset) return;
            const value = f.kind === 'checkbox' ? el.checked : el.value.trim();
            const isDefault = f.kind === 'color'
                ? !value || value.toLowerCase() === 'auto'
                : String(value) === String(f.default);
            reset.style.display = isDefault ? 'none' : 'inline';
        });
    }
    function sync() {
        const grid = input('layout').value === 'grid';
        const block = input('shape').value === 'block';
        input('columns-wrap').hidden = !grid;
        input('aspect-wrap').hidden = !grid || !block;
        input('thickness-wrap').hidden = grid || !block;
        input('rounding-wrap').hidden = !block;
        input('gap-y-wrap').hidden = input('gaps-linked').checked;
        input('inactive-options').hidden = input('inactive-style').value === 'hidden';
        input('reshuffle-wrap').hidden = input('order').value !== 'random';
        syncResets();
    }
    function setValues(values) {
        Object.entries(values).forEach(([name, value]) => {
            input(name).value = name.endsWith('-color') && value === 'auto' ? '' : String(value);
        });
        if ('gap-x' in values || 'gap-y' in values)
            input('gaps-linked').checked = input('gap-x').value === input('gap-y').value;
        syncColors();
        sync();
        input('stagger').dispatchEvent(new Event('input', { bubbles: true }));
    }
    fields.filter(f => f.kind === 'color').forEach(f => {
        f.preview = () => {
            const el = document.getElementById(f.id);
            if (el.value.trim().toLowerCase() === 'auto') el.value = '';
            syncColors();
        };
    });
    fields[fields.length - 1].preview = sync;
    function numberOrBinding(name, allowBlank) {
        const value = input(name).value.trim();
        if (!value && allowBlank) return null;
        if (new TextEncoder().encode(value).length > (name === 'current' ? 191 : 63))
            fail(name, 'Value is too long');
        if (value.includes('[')) return null;
        if (!/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$/.test(value) ||
            !Number.isFinite(Number(value))) fail(name, 'Enter a finite number or binding');
        return Number(value);
    }
    function autoInteger(button, name, key, minimum, maximum) {
        const value = input(name).value.trim().toLowerCase();
        if (!value || value === 'auto') { button[key] = 'auto'; return; }
        if (!/^\d+$/.test(value) || Number(value) < minimum || Number(value) > maximum)
            fail(name, 'Enter Auto or a whole number from ' + minimum + ' to ' + maximum);
        button[key] = Number(value);
    }
    function build(button) {
        button.widget_progress_match_spacing = input('gaps-linked').checked;
        numberOrBinding('current', true);
        const min = numberOrBinding('min', false);
        const max = numberOrBinding('max', false);
        const units = numberOrBinding('units', false);
        if (min !== null && max !== null && max <= min) fail('max', 'Maximum must exceed minimum');
        if (units !== null && (!Number.isInteger(units) || units < 1 || units > 1024))
            fail('units', 'Unit count must be a whole number from 1 to 1024');
        for (const [name, key, minValue, maxValue] of [
            ['columns', 'widget_progress_columns', 1, 1024],
            ['thickness', 'widget_progress_thickness', 1, 100],
            ['gap-x', 'widget_progress_gap_x', 0, 256],
            ['gap-y', 'widget_progress_gap_y', 0, 256]
        ]) autoInteger(button, name, key, minValue, maxValue);
        for (const name of ['active-color', 'inactive-color']) {
            const value = input(name).value.trim();
            if (new TextEncoder().encode(value).length > 191 ||
                (value && value !== 'auto' && !/^#[0-9a-f]{6}$/i.test(value) && !value.includes('[')))
                fail(name, 'Enter Auto, a #RRGGBB color, or a binding');
        }
        for (const [name, minimum, maximum, integer] of [
            ['aspect', 0.1, 10, false], ['rounding', 0, 50, true],
            ['inactive-size', 1, 100, true], ['duration', 0, 5000, true],
            ['stagger', 0, 1000, true], ['seed', 0, 4294967295, true]
        ]) {
            const value = Number(input(name).value);
            if (!Number.isFinite(value) || value < minimum || value > maximum ||
                (integer && !Number.isInteger(value))) fail(name, 'Value is outside the supported range');
        }
    }
    padRegisterWidgetEditor({
        type: 'progress',
        sections: [{ id: prefix + 'section', tab: 'Content' }],
        fields,
        actions: { tap: true, longPress: true },
        load(button) {
            const x = button.widget_progress_gap_x ?? 'auto';
            const y = button.widget_progress_gap_y ?? 'auto';
            input('gaps-linked').checked = button.widget_progress_match_spacing !== undefined
                ? button.widget_progress_match_spacing && String(x) === String(y)
                : String(x) === String(y);
            input('preset').value = '';
        },
        init() {
            fields.filter(f => f.id !== prefix + 'seed').forEach(f => {
                const el = document.getElementById(f.id);
                const label = document.querySelector('label[for="' + f.id + '"]') || el.closest('label');
                const reset = document.createElement('button');
                reset.type = 'button';
                reset.id = f.id + '-reset';
                reset.className = 'reset-hint material-symbols-outlined list-color-reset';
                reset.title = 'Reset to default';
                const title = label.querySelector('.binding-field-title') || label;
                reset.setAttribute('aria-label', 'Reset ' + title.textContent.replace(/\s*fx\s*$/, '').trim() + ' to default');
                reset.textContent = 'restart_alt';
                reset.addEventListener('click', event => {
                    event.preventDefault();
                    if (f.kind === 'color') padSetBindableColor(f.id, '');
                    else {
                        if (f.kind === 'checkbox') el.checked = f.default;
                        else el.value = String(f.default);
                        el.dispatchEvent(new Event(f.kind === 'checkbox' || el.tagName === 'SELECT' ? 'change' : 'input', { bubbles: true }));
                    }
                    sync();
                });
                label.appendChild(reset);
                el.addEventListener('input', syncResets);
                el.addEventListener('change', syncResets);
                if (f.kind === 'color') {
                    padInitBindableColor(el.closest('.bindable-color'));
                    el.addEventListener('input', syncColors);
                }
            });
            const foreground = document.getElementById('pad-edit-fg-color');
            if (!foreground.dataset.progressColorsInit) {
                foreground.dataset.progressColorsInit = '1';
                foreground.addEventListener('input', syncColors);
            }
            for (const name of ['layout', 'shape', 'inactive-style', 'order', 'gaps-linked'])
                input(name).addEventListener('change', sync);
            input('gap-x').addEventListener('input', () => {
                if (input('gaps-linked').checked) input('gap-y').value = input('gap-x').value;
            });
            input('gaps-linked').addEventListener('change', () => {
                if (input('gaps-linked').checked) input('gap-y').value = input('gap-x').value;
            });
            input('reshuffle').addEventListener('click', () => {
                const old = Number(input('seed').value) >>> 0;
                let next = Math.floor(Math.random() * 4294967296) >>> 0;
                if (next === old) next = (next + 1) >>> 0;
                input('seed').value = String(next);
                input('seed').dispatchEvent(new Event('input', { bubbles: true }));
            });
            input('preset').addEventListener('change', () => {
                const recipe = recipes[input('preset').value];
                input('preset').value = '';
                if (!recipe || !confirm('Apply this sample look? Visual settings, including color bindings, will be replaced. Current value, range, inversion, and unit count will be kept.')) return;
                setValues({ ...base, ...recipe });
            });
        },
        build
    });
})();
