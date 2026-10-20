function padButtonBuilderDecimalField() {
    return { name: 'decimals', label: 'Decimal places', type: 'number', default: 1, min: 0, max: 6,
        error: 'Choose 0 to 6 decimal places.' };
}

function padButtonBuilderUnitField(unit) {
    return { name: 'unit', label: 'Unit', type: 'text', default: unit || '', maxlength: 60 };
}

function padButtonBuilderRangeFields(detail) {
    const value = Number(detail.state);
    const percentage = detail.unit?.trim() === '%';
    let max = 100;
    if (!percentage && value !== 0) {
        const target = Math.abs(value) * 2;
        const scale = 10 ** Math.floor(Math.log10(target));
        max = [1, 2, 5, 10].find(step => step * scale >= target) * scale;
    }
    return [
        { name: 'min', label: 'Minimum', type: 'number', step: 'any',
            default: !percentage && value < 0 ? -max : 0, error: 'Enter a numeric minimum and maximum.' },
        { name: 'max', label: 'Maximum', type: 'number', step: 'any',
            default: max, error: 'Enter a numeric minimum and maximum.' }
    ];
}

function padButtonBuilderRange(options) {
    const min = Number(options.min), max = Number(options.max);
    const range = Math.fround(Math.fround(max) - Math.fround(min));
    if (!Number.isFinite(range) || range <= 0) {
        throw new Error('Maximum must be greater than minimum within the supported numeric range.');
    }
    return { min, max };
}

function padButtonBuilderHistorySourceAvailable({ detail }) {
    return !detail.state_truncated && (bindingBuilderIsNumeric(detail.state) ||
        (['unknown', 'unavailable'].includes(detail.state) && !!detail.unit));
}

function padButtonBuilderHistorySampling({ window: range, interval }) {
    const windowSeconds = Number(range);
    const slots = Math.max(2, Math.min(1024, Math.ceil(windowSeconds / Number(interval))));
    return { windowSeconds, slots, interval: windowSeconds / slots };
}

// Templates own their controls, configuration, and preview overrides.
const padButtonBuilders = [
    {
        id: 'value', label: 'Show a value',
        description: 'Show the entity name, current value, and unit.',
        note: 'The unit is display text only; changing it does not convert the value.',
        available: () => true,
        fields: ({ sources: [{ detail }] }) => [
            ...(bindingBuilderIsNumeric(detail.state) || detail.unit ? [padButtonBuilderDecimalField()] : []),
            padButtonBuilderUnitField(detail.unit)
        ],
        build({ sources: [{ detail }], options }) {
            const numeric = bindingBuilderIsNumeric(detail.state) || !!detail.unit;
            const format = bindingBuilderFormat(numeric ? 'number' : 'text', Number(options.decimals), '');
            return {
                label_center: bindingBuilderToken('ha', detail.entity_id, '', format, ''),
                label_bottom: options.unit.trim()
            };
        },
        preview({ sources: [{ detail }], options }) {
            return { label_center: ['unknown', 'unavailable'].includes(detail.state) ? '---' :
                bindingBuilderSample(detail.state, bindingBuilderIsNumeric(detail.state) ? 'number' : 'text',
                    Number(options.decimals), '') };
        }
    },
    {
        id: 'bar', label: 'Show a bar',
        description: 'Show a numeric value as a single bar within a fixed range.',
        note: 'Check the suggested range before creating the button. The unit changes display text, not the value. Fine-tune styling in the pad editor.',
        available: ({ sources: [{ detail }] }) => !detail.state_truncated && bindingBuilderIsNumeric(detail.state) &&
            !!deviceInfoCache?.widget_catalog?.some(widget => widget.type === 'bar_chart'),
        fields({ sources: [{ detail }] }) {
            return [
                ...padButtonBuilderRangeFields(detail),
                { name: 'orientation', label: 'Orientation', type: 'select', default: 'vertical',
                    options: [['vertical', 'Vertical'], ['horizontal', 'Horizontal']],
                    error: 'Choose a valid orientation.' },
                padButtonBuilderDecimalField(),
                padButtonBuilderUnitField(detail.unit)
            ];
        },
        build({ sources: [{ detail }], options }) {
            const { min, max } = padButtonBuilderRange(options);
            const format = bindingBuilderFormat('number', Number(options.decimals), bindingBuilderUnitSuffix(options.unit));
            return {
                widget_type: 'bar_chart',
                widget_data_binding: bindingBuilderToken('ha', detail.entity_id, '', '', ''),
                widget_bar_min: min, widget_bar_max: max,
                widget_orientation: options.orientation,
                widget_bar_zero_centered: min < 0 && max > 0,
                label_bottom: bindingBuilderToken('ha', detail.entity_id, '', format, '')
            };
        },
        preview({ sources: [{ detail }], options }) {
            return { label_bottom: bindingBuilderIsNumeric(detail.state) ?
                bindingBuilderSample(detail.state, 'number', Number(options.decimals), bindingBuilderUnitSuffix(options.unit)) : '---' };
        },
        renderPreview({ sources: [{ detail }] }, cell, button) {
            const bar = document.createElement('div');
            bar.className = 'pad-button-builder-bar';
            bar.dataset.orientation = button.widget_orientation;
            const track = document.createElement('div');
            track.className = 'pad-button-builder-bar-track';
            track.style.background = PAD_FIRMWARE_DEFAULTS.widget_track_color;
            const fill = document.createElement('div');
            fill.className = 'pad-button-builder-bar-fill';
            fill.style.background = PAD_FIRMWARE_DEFAULTS.widget_series_color;
            if (bindingBuilderIsNumeric(detail.state)) {
                const min = button.widget_bar_min, max = button.widget_bar_max;
                const value = Math.max(min, Math.min(max, Number(detail.state)));
                const baseline = button.widget_bar_zero_centered ? 0 : min;
                bar.setAttribute('role', 'meter');
                bar.setAttribute('aria-label', button.label_top || 'Entity value');
                bar.setAttribute('aria-valuemin', String(min));
                bar.setAttribute('aria-valuemax', String(max));
                bar.setAttribute('aria-valuenow', String(value));
                bar.setAttribute('aria-valuetext', button.label_bottom);
                track.style.setProperty('--bar-start', (Math.min(value, baseline) - min) / (max - min) * 100 + '%');
                track.style.setProperty('--bar-size', Math.abs(value - baseline) / (max - min) * 100 + '%');
            } else {
                bar.setAttribute('role', 'img');
                bar.setAttribute('aria-label', 'Bar value unavailable');
                fill.hidden = true;
            }
            track.appendChild(fill);
            bar.appendChild(track);
            cell.insertBefore(bar, cell.querySelector('.pad-cell-label-bottom'));
        }
    },
    {
        id: 'gauge', label: 'Show a gauge',
        description: 'Show a numeric value as a single ring with a needle within a fixed range.',
        note: 'Check the suggested range before creating the button. The unit changes display text, not the value. Fine-tune styling in the pad editor.',
        available: ({ sources: [{ detail }] }) => !detail.state_truncated && bindingBuilderIsNumeric(detail.state) &&
            !!deviceInfoCache?.widget_catalog?.some(widget => widget.type === 'gauge'),
        fields: ({ sources: [{ detail }] }) => [
            ...padButtonBuilderRangeFields(detail),
            padButtonBuilderDecimalField(),
            padButtonBuilderUnitField(detail.unit),
            { name: 'color', label: 'Arc color', type: 'color', default: PAD_FIRMWARE_DEFAULTS.widget_series_color,
                error: 'Choose a valid arc color.' }
        ],
        build({ sources: [{ detail }], options }) {
            const { min, max } = padButtonBuilderRange(options);
            const format = bindingBuilderFormat('number', Number(options.decimals), bindingBuilderUnitSuffix(options.unit));
            return {
                widget_type: 'gauge',
                widget_data_binding: bindingBuilderToken('ha', detail.entity_id, '', '', ''),
                widget_gauge_min: min, widget_gauge_max: max,
                widget_gauge_degrees: 180, widget_gauge_start_angle: 180,
                widget_gauge_show_needle: true, widget_gauge_ticks: 5,
                widget_arc_color: options.color,
                label_bottom: bindingBuilderToken('ha', detail.entity_id, '', format, '')
            };
        },
        preview({ sources: [{ detail }], options }) {
            return { label_bottom: bindingBuilderIsNumeric(detail.state) ?
                bindingBuilderSample(detail.state, 'number', Number(options.decimals), bindingBuilderUnitSuffix(options.unit)) : '---' };
        },
        previewNote: () => 'Approximate gauge using the fetched value. Preview only. Does not control the entity.',
        renderPreview({ sources: [{ detail }] }, cell, button) {
            const gauge = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
            gauge.setAttribute('class', 'pad-button-builder-gauge');
            gauge.setAttribute('viewBox', '0 0 280 145');
            const track = document.createElementNS('http://www.w3.org/2000/svg', 'path');
            track.setAttribute('d', 'M28 130 A112 112 0 0 1 252 130');
            track.setAttribute('fill', 'none');
            track.setAttribute('stroke', PAD_FIRMWARE_DEFAULTS.widget_track_color);
            track.setAttribute('stroke-width', '17');
            gauge.appendChild(track);
            if (bindingBuilderIsNumeric(detail.state)) {
                const min = button.widget_gauge_min, max = button.widget_gauge_max;
                const value = Math.max(min, Math.min(max, Number(detail.state)));
                const progress = (value - min) / (max - min);
                gauge.setAttribute('role', 'meter');
                gauge.setAttribute('aria-label', button.label_top || 'Entity value');
                gauge.setAttribute('aria-valuemin', String(min));
                gauge.setAttribute('aria-valuemax', String(max));
                gauge.setAttribute('aria-valuenow', String(value));
                gauge.setAttribute('aria-valuetext', button.label_bottom);
                const fill = document.createElementNS('http://www.w3.org/2000/svg', 'path');
                fill.setAttribute('d', 'M28 130 A112 112 0 0 1 252 130');
                fill.setAttribute('fill', 'none');
                fill.setAttribute('stroke', button.widget_arc_color);
                fill.setAttribute('stroke-width', '17');
                fill.setAttribute('pathLength', '100');
                fill.setAttribute('stroke-dasharray', progress * 100 + ' 100');
                gauge.appendChild(fill);
                const needle = document.createElementNS('http://www.w3.org/2000/svg', 'line');
                needle.setAttribute('x1', '140');
                needle.setAttribute('y1', '130');
                needle.setAttribute('x2', '42');
                needle.setAttribute('y2', '130');
                needle.setAttribute('stroke', PAD_FIRMWARE_DEFAULTS.widget_indicator_color);
                needle.setAttribute('stroke-width', '2');
                needle.setAttribute('transform', 'rotate(' + progress * 180 + ' 140 130)');
                gauge.appendChild(needle);
            } else {
                gauge.setAttribute('role', 'img');
                gauge.setAttribute('aria-label', 'Gauge value unavailable');
            }
            for (let tick = 1; tick <= button.widget_gauge_ticks; tick++) {
                const angle = Math.PI + tick * Math.PI / (button.widget_gauge_ticks + 1);
                const line = document.createElementNS('http://www.w3.org/2000/svg', 'line');
                line.setAttribute('x1', String(140 + Math.cos(angle) * 95));
                line.setAttribute('y1', String(130 + Math.sin(angle) * 95));
                line.setAttribute('x2', String(140 + Math.cos(angle) * 87));
                line.setAttribute('y2', String(130 + Math.sin(angle) * 87));
                line.setAttribute('stroke', PAD_FIRMWARE_DEFAULTS.widget_tick_color);
                line.setAttribute('stroke-width', '1');
                gauge.appendChild(line);
            }
            cell.insertBefore(gauge, cell.querySelector('.pad-cell-label-bottom'));
        }
    },
    {
        id: 'history', label: 'Show a value with history',
        description: 'Show up to three current values and their trends.',
        note: 'Units change display text, not values. Automatic scale shares a scale for matching units; otherwise each value has its own scale, so line heights are not directly comparable. Sampling is limited to 1024 points per line. Without Home Assistant history support, the device collects live values while it runs. Resize the button in the editor for more space.',
        maxSources: 3,
        available: ({ sources: [source] }) => padButtonBuilderHistorySourceAvailable(source) &&
            !!deviceInfoCache?.widget_catalog?.some(widget => widget.type === 'sparkline'),
        validateSource(source) {
            if (!padButtonBuilderHistorySourceAvailable(source)) {
                throw new Error('Choose an entity with a numeric, non-truncated state or an unavailable reading with a unit.');
            }
        },
        fields: () => [
            { name: 'window', label: 'Time range', type: 'select', default: '86400',
                options: [['3600', '1 hour'], ['18000', '5 hours'], ['21600', '6 hours'], ['86400', '24 hours'], ['604800', '7 days']],
                error: 'Choose a valid time range.' },
            { name: 'interval', label: 'Requested sample interval (seconds)', type: 'number',
                default: 300, step: 'any', min: 0.1, max: 604800, error: 'Enter a sample interval between 0.1 seconds and 7 days.' },
            { name: 'scale', label: 'Scale', type: 'select', default: 'auto',
                options: [['auto', 'Automatic (based on entity units)'], ['shared', 'Shared scale'],
                    ['independent', 'Independent per value']], error: 'Choose a valid scale.' }
        ],
        sourceFields: ({ detail }, index) => [
            padButtonBuilderDecimalField(),
            padButtonBuilderUnitField(detail.unit),
            { name: 'color', label: 'Line color', type: 'color', default: ['#4CAF50', '#2196F3', '#9C27B0'][index],
                error: 'Choose a valid line color.' }
        ],
        sourceOptions({ detail }, previous) {
            return previous?.detail.entity_id === detail.entity_id ? {} : { unit: detail.unit || '' };
        },
        validateOutput({ sources }, button) {
            sources.forEach((source, index) => {
                const suffix = index ? '_' + (index + 1) : '';
                padButtonBuilderValidateText(button['widget_sparkline_current_label' + suffix], 63);
                padButtonBuilderValidateText(button['widget_sparkline_ha_entity' + suffix], 63);
            });
        },
        build({ options, sources }) {
            const { windowSeconds, slots } = padButtonBuilderHistorySampling(options);
            const units = new Set(sources.map(source => (source.detail.unit || '').trim()));
            const button = {
                label_top: '',
                label_center: options.title.trim(),
                label_center_style: 'align:left',
                widget_type: 'sparkline',
                widget_sparkline_line_width: 2,
                widget_sparkline_current_dot: 5,
                widget_sparkline_smooth: 0,
                widget_sparkline_label_width: 100,
                widget_sparkline_unified_scale: options.scale === 'shared' || (options.scale === 'auto' && units.size === 1),
                widget_sparkline_window: windowSeconds,
                widget_sparkline_slots: slots
            };
            sources.forEach((source, index) => {
                const suffix = index ? '_' + (index + 1) : '';
                const format = bindingBuilderFormat('number', Number(source.options.decimals), bindingBuilderUnitSuffix(source.options.unit));
                button['widget_data_binding' + suffix] = bindingBuilderToken('ha', source.detail.entity_id, '', '', '');
                button['widget_sparkline_current_label' + suffix] = bindingBuilderToken('ha', source.detail.entity_id, '', format, '');
                button['widget_sparkline_line_color' + suffix] = source.options.color;
                if ((window.__device_caps || {}).ha_history === true) {
                    button['widget_sparkline_ha_entity' + suffix] = source.detail.entity_id;
                    button['widget_sparkline_ha_stat' + suffix] = 'mean';
                }
            });
            return button;
        },
        preview() {
            return { widget_type: '' };
        },
        previewNote({ options }) {
            const hours = Number(options.window) / 3600;
            const range = hours === 168 ? '7 days' : hours + (hours === 1 ? ' hour' : ' hours');
            const sampling = padButtonBuilderHistorySampling(options);
            const slots = sampling.slots;
            const interval = Number(sampling.interval.toFixed(2));
            return 'Last ' + range + '. Actual sample interval: ' + interval + ' seconds (' + slots +
                ' points per line). Illustrative trend, not actual history. Preview only. Does not control the entity.';
        },
        renderPreview({ sources }, cell, button) {
            const chart = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
            chart.setAttribute('class', 'pad-button-builder-sparkline');
            chart.setAttribute('viewBox', '0 0 280 70');
            chart.setAttribute('role', 'img');
            const curves = [
                '8,54 22,50 36,52 50,30 64,26 78,40 92,44 106,24 120,18 134,22 148,38 162,36 176,20 200,12',
                '8,18 22,22 36,30 50,28 64,36 78,30 92,20 106,26 120,38 134,42 148,40 162,48 176,44 200,54',
                '8,36 22,32 36,38 50,44 64,42 78,50 92,46 106,38 120,30 134,34 148,22 162,28 176,32 200,33'
            ];
            const descriptions = [];
            sources.forEach((source, index) => {
                const [x, y] = curves[index].split(' ').at(-1).split(',');
                const numeric = bindingBuilderIsNumeric(source.detail.state);
                const sample = numeric ?
                    bindingBuilderSample(source.detail.state, 'number', Number(source.options.decimals),
                        bindingBuilderUnitSuffix(source.options.unit)) : '---';
                descriptions.push(source.name + ': ' + sample);
                if (numeric) {
                    const line = document.createElementNS('http://www.w3.org/2000/svg', 'polyline');
                    line.setAttribute('points', curves[index]);
                    line.setAttribute('fill', 'none');
                    line.setAttribute('stroke', source.options.color);
                    line.setAttribute('stroke-width', String(button.widget_sparkline_line_width));
                    const dot = document.createElementNS('http://www.w3.org/2000/svg', 'circle');
                    dot.setAttribute('cx', x);
                    dot.setAttribute('cy', y);
                    dot.setAttribute('r', String(button.widget_sparkline_current_dot / 2));
                    dot.setAttribute('fill', source.options.color);
                    chart.appendChild(line);
                    chart.appendChild(dot);
                }
                const value = document.createElementNS('http://www.w3.org/2000/svg', 'text');
                value.setAttribute('x', String(Number(x) + 8));
                value.setAttribute('y', y);
                value.setAttribute('dominant-baseline', 'middle');
                value.setAttribute('font-size', '12');
                value.setAttribute('font-family', 'inherit');
                value.setAttribute('fill', source.options.color);
                value.textContent = sample;
                chart.appendChild(value);
            });
            chart.setAttribute('aria-label', 'Illustrative trend, not actual history. ' + descriptions.join('; '));
            cell.appendChild(chart);
        }
    },
    {
        id: 'toggle', label: 'Toggle an entity',
        description: 'Toggle a light or switch and show whether it is on or off.',
        note: 'Icon color follows the entity state. Unavailable data uses a pink-red icon.',
        preferred: true,
        available: ({ sources: [{ detail }] }) => /^(light|switch)\./.test(detail.entity_id) && !!actionEditorCatalogEntry('ha_service'),
        fields: ({ sources: [{ detail }] }) => [
            { name: 'icon', label: 'Material Symbol name', type: 'text', maxlength: 28, required: true,
                default: detail.entity_id.startsWith('light.') ? 'lightbulb' : 'toggle_on',
                link: { label: 'Browse Material Symbols', href: 'https://fonts.google.com/icons' } },
            { name: 'on', label: 'On color', type: 'color', default: '#FFC857',
                error: 'Choose valid on and off colors.' },
            { name: 'off', label: 'Off color', type: 'color', default: '#64748B',
                error: 'Choose valid on and off colors.' }
        ],
        build({ sources: [{ detail }], options }) {
            if (detail.entity_id.length >= 48) throw new Error('Entity IDs must be shorter than 48 characters for service actions.');
            if (!/^[a-z][a-z0-9_]{0,27}$/.test(options.icon)) throw new Error('Enter a Material Symbol name, such as lightbulb.');
            const state = bindingBuilderToken('ha', detail.entity_id, '', '', '');
            return {
                icon_id: 'mi_' + options.icon, icon_position: 'center',
                label_top_style: 'color:' + (padColorNormalizeLiteral(padGetEffectiveDefault('fg_color')) || '#FFFFFF'),
                fg_color: '[expr:' + state + '=="on"?"' + options.on + '":"' + options.off + '"|#EF6B73]',
                actions: [{ type: 'ha_service', entity_id: detail.entity_id, service: 'toggle' }]
            };
        },
        preview({ options, previewState }) {
            return { fg_color: previewState === 'on' ? options.on : previewState === 'off' ? options.off : '#EF6B73' };
        },
        previewStates: [['on', 'On'], ['off', 'Off'], ['unavailable', 'Unavailable']]
    }
];
