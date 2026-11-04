(() => {
    const prefix = 'pad-edit-slider-';
    const input = name => document.getElementById(prefix + name);
    const fail = (name, message) => {
        throw Object.assign(new Error(message), { fieldIds: [prefix + name] });
    };

    function fixed(name, maximum = 1000000) {
        const raw = input(name).value.trim();
        if (!raw || !/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$/.test(raw))
            fail(name, 'Enter a finite decimal with at most four fractional digits');
        const number = Number(raw);
        const scaled = number * 10000;
        const rounded = Math.round(scaled);
        const tolerance = Number.EPSILON * Math.max(1, Math.abs(scaled)) * 4;
        if (!Number.isFinite(number) || Math.abs(number) > maximum ||
            Math.abs(scaled - rounded) > tolerance)
            fail(name, 'Value must be finite, use at most four decimal places, and stay within +/-' + maximum);
        return rounded;
    }

    function range() {
        const minimum = fixed('min');
        const maximum = fixed('max');
        const step = fixed('step', 2000000);
        const span = maximum - minimum;
        if (maximum <= minimum || step <= 0 || span % step !== 0 || span / step > 10000)
            fail('step', 'Range must divide evenly into 1 to 10000 steps');
        return { minimum, maximum, step, intervals: span / step };
    }

    function selectedAxis() {
        return input('axis').value || 'horizontal';
    }

    const fields = [
        { id: prefix + 'source', key: 'widget_data_binding', kind: 'text', default: '', binding: { isWidgetBinding: true } },
        { id: prefix + 'axis', key: 'widget_slider_axis', kind: 'text', default: 'horizontal' },
        { id: prefix + 'min', key: 'widget_slider_min', kind: 'number', default: 0 },
        { id: prefix + 'max', key: 'widget_slider_max', kind: 'number', default: 100 },
        { id: prefix + 'step', key: 'widget_slider_step', kind: 'number', default: 1 },
        { id: prefix + 'length', key: 'widget_slider_length_pct', kind: 'number', default: 50 },
        { id: prefix + 'icon-position', key: 'widget_slider_icon_position', kind: 'text', default: 'left' },
        { id: prefix + 'track', key: 'widget_slider_track_px', kind: 'number', default: 0 },
        { id: prefix + 'knob', key: 'widget_slider_knob_px', kind: 'number', default: 0 },
        { id: prefix + 'track-color', key: 'widget_slider_track_color', kind: 'color', default: () => PAD_FIRMWARE_DEFAULTS.widget_track_color },
        { id: prefix + 'fill-color', key: 'widget_slider_fill_color', kind: 'color', default: () => PAD_FIRMWARE_DEFAULTS.widget_series_color },
        { id: prefix + 'knob-color', key: 'widget_slider_knob_color', kind: 'color', default: () => PAD_FIRMWARE_DEFAULTS.widget_indicator_color },
        { id: prefix + 'show-value', key: 'widget_slider_show_value', kind: 'checkbox', default: true },
        { id: prefix + 'value-font-size', key: 'widget_slider_value_font_size', kind: 'number', default: 0 },
        { id: prefix + 'value-color', key: 'widget_slider_value_color', kind: 'color', default: '' },
        { id: prefix + 'value-format', key: 'widget_slider_value_format', kind: 'text', default: '' },
        { id: prefix + 'value-clearance', key: 'widget_slider_value_clearance', kind: 'number', default: 40 },
        { id: prefix + 'value-side', key: 'widget_slider_value_side', kind: 'text', default: 'auto' }
    ];

    padRegisterWidgetEditor({
        type: 'slider',
        available: info => !!(info && info.has_display === true && info.has_touch === true),
        sections: [
            { id: 'pad-edit-slider-section', tab: 'Content' },
            { id: 'pad-edit-slider-value-section', tab: 'Content' },
            { id: 'pad-edit-slider-action-note', tab: 'Actions' }
        ],
        fields,
        icon: { fixedPosition: true, width: 0.4, height: 0.4 },
        actions: {
            tap: true, longPress: false,
            labels: () => ['Value set action', 'Long-press action'],
            buildTap() {
                return actionEditorListBuild(padActionPrefixes('tap'), '{value}');
            }
        },
        build(button) {
            const axis = selectedAxis();
            if (axis !== 'horizontal' && axis !== 'vertical') fail('min', 'Choose horizontal or vertical orientation');
            range();
            const length = Number(input('length').value);
            if (!Number.isInteger(length) || length < 20 || length > 100)
                fail('length', 'Length must be a whole number from 20 to 100');
            if (!['left', 'above', 'below'].includes(button.widget_slider_icon_position))
                fail('icon-position', 'Choose left, above, or below for the icon');
            const track = Number(input('track').value);
            if (!Number.isInteger(track) || track < 0 || track > 32)
                fail('track', 'Track size must be 0 or a whole number from 1 to 32');
            const knob = Number(input('knob').value);
            if (!Number.isInteger(knob) || knob < 0 || knob > 64 || (knob > 0 && knob < 8))
                fail('knob', 'Knob size must be 0 or a whole number from 8 to 64');
            for (const name of ['track-color', 'fill-color', 'knob-color']) {
                if (!/^#[0-9a-f]{6}$/i.test(input(name).value.trim())) fail(name, 'Choose a #RRGGBB color');
            }
            button.widget_slider_axis = axis;
            if (!Number.isInteger(button.widget_slider_value_font_size) ||
                button.widget_slider_value_font_size < 0 || button.widget_slider_value_font_size > 512)
                fail('value-font-size', 'Font size must be a whole number from 0 to 512');
            const valueColor = button.widget_slider_value_color || '';
            if (valueColor && !/^#[0-9a-f]{6}$/i.test(valueColor))
                fail('value-color', 'Choose a #RRGGBB color or leave empty for Auto');
            if (!['auto', 'before', 'after'].includes(button.widget_slider_value_side))
                fail('value-side', 'Choose an available value position');
            const format = input('value-format').value;
            if (new TextEncoder().encode(format).length > 64)
                fail('value-format', 'Value format must fit within 64 UTF-8 bytes');
            if (format) {
                let conversions = 0;
                const remaining = format.replace(/%%|%([-+ #0]*)(\d*)(?:\.(\d*))?([lL]?)([fFs])/g,
                    (token, flags, width, precision, length, type) => {
                        if (token === '%%') return '';
                        conversions++;
                        if (Number(width) > 32 || Number(precision) > 8 ||
                            (type === 's' && (length || /[^-]/.test(flags))))
                            fail('value-format', 'Use a width up to 32 and precision up to 8; %s only supports the - flag');
                        return '';
                    });
                if (conversions !== 1 || remaining.includes('%'))
                    fail('value-format', 'Use one %f or %s conversion and %% for a literal percent');
            }
            button.widget_slider_value_format = format;
            const clearance = Number(input('value-clearance').value);
            if (!Number.isInteger(clearance) || clearance < 0 || clearance > 200)
                fail('value-clearance', 'Knob clearance must be a whole number from 0 to 200');
            button.widget_slider_value_clearance = clearance;
        }
    });
})();