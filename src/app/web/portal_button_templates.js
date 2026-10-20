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

// Templates own their controls, configuration, and preview overrides.
const padButtonBuilders = [
    {
        id: 'value', label: 'Show a value',
        description: 'Show the entity name, current value, and unit.',
        note: 'The unit is display text only; changing it does not convert the value.',
        available: () => true,
        fields: detail => [
            ...(bindingBuilderIsNumeric(detail.state) || detail.unit ? [padButtonBuilderDecimalField()] : []),
            padButtonBuilderUnitField(detail.unit)
        ],
        build(detail, options) {
            const numeric = bindingBuilderIsNumeric(detail.state) || !!detail.unit;
            const format = bindingBuilderFormat(numeric ? 'number' : 'text', Number(options.decimals), '');
            return {
                label_center: bindingBuilderToken('ha', detail.entity_id, '', format, ''),
                label_bottom: options.unit.trim()
            };
        },
        preview(detail, options) {
            return { label_center: ['unknown', 'unavailable'].includes(detail.state) ? '---' :
                bindingBuilderSample(detail.state, bindingBuilderIsNumeric(detail.state) ? 'number' : 'text',
                    Number(options.decimals), '') };
        }
    },
    {
        id: 'bar', label: 'Show a bar',
        description: 'Show a numeric value as a single bar within a fixed range.',
        note: 'Check the suggested range before creating the button. The unit changes display text, not the value. Fine-tune styling in the pad editor.',
        available: detail => !detail.state_truncated && bindingBuilderIsNumeric(detail.state) &&
            !!deviceInfoCache?.widget_catalog?.some(widget => widget.type === 'bar_chart'),
        fields(detail) {
            return [
                ...padButtonBuilderRangeFields(detail),
                { name: 'orientation', label: 'Orientation', type: 'select', default: 'vertical',
                    options: [['vertical', 'Vertical'], ['horizontal', 'Horizontal']],
                    error: 'Choose a valid orientation.' },
                padButtonBuilderDecimalField(),
                padButtonBuilderUnitField(detail.unit)
            ];
        },
        build(detail, options) {
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
        preview(detail, options) {
            return { label_bottom: bindingBuilderIsNumeric(detail.state) ?
                bindingBuilderSample(detail.state, 'number', Number(options.decimals), bindingBuilderUnitSuffix(options.unit)) : '---' };
        },
        renderPreview(cell, button, detail) {
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
        available: detail => !detail.state_truncated && bindingBuilderIsNumeric(detail.state) &&
            !!deviceInfoCache?.widget_catalog?.some(widget => widget.type === 'gauge'),
        fields: detail => [
            ...padButtonBuilderRangeFields(detail),
            padButtonBuilderDecimalField(),
            padButtonBuilderUnitField(detail.unit),
            { name: 'color', label: 'Arc color', type: 'color', default: PAD_FIRMWARE_DEFAULTS.widget_series_color,
                error: 'Choose a valid arc color.' }
        ],
        build(detail, options) {
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
        preview(detail, options) {
            return { label_bottom: bindingBuilderIsNumeric(detail.state) ?
                bindingBuilderSample(detail.state, 'number', Number(options.decimals), bindingBuilderUnitSuffix(options.unit)) : '---' };
        },
        previewNote: () => 'Approximate gauge using the fetched value. Preview only. Does not control the entity.',
        renderPreview(cell, button, detail) {
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
        description: 'Show the current numeric value with a sparkline.',
        note: 'The unit changes display text, not the value. Without Home Assistant history support, the device collects live values while it runs. Fine-tune sampling and styling in the pad editor.',
        available: detail => !detail.state_truncated && bindingBuilderIsNumeric(detail.state) &&
            !!deviceInfoCache?.widget_catalog?.some(widget => widget.type === 'sparkline'),
        fields: detail => [
            padButtonBuilderDecimalField(),
            padButtonBuilderUnitField(detail.unit),
            { name: 'window', label: 'Time range', type: 'select', default: '86400',
                options: [['3600', '1 hour'], ['21600', '6 hours'], ['86400', '24 hours'], ['604800', '7 days']],
                error: 'Choose a valid time range.' },
            { name: 'color', label: 'Line color', type: 'color', default: '#4CAF50',
                error: 'Choose a valid line color.' }
        ],
        build(detail, options) {
            const windowSeconds = Number(options.window);
            const format = bindingBuilderFormat('number', Number(options.decimals), bindingBuilderUnitSuffix(options.unit));
            return {
                label_center: bindingBuilderToken('ha', detail.entity_id, '', format, ''),
                widget_type: 'sparkline',
                widget_data_binding: bindingBuilderToken('ha', detail.entity_id, '', '', ''),
                widget_sparkline_line_color: options.color,
                widget_sparkline_line_width: 2,
                widget_sparkline_current_dot: 5,
                widget_sparkline_smooth: 0,
                widget_sparkline_unified_scale: true,
                widget_sparkline_window: windowSeconds,
                widget_sparkline_slots: Math.min(1024, Math.max(120, Math.ceil(windowSeconds / 300))),
                ...((window.__device_caps || {}).ha_history === true ? {
                    widget_sparkline_ha_entity: detail.entity_id,
                    widget_sparkline_ha_stat: 'mean'
                } : {})
            };
        },
        preview(detail, options) {
            return { widget_type: '', label_center: bindingBuilderIsNumeric(detail.state) ?
                bindingBuilderSample(detail.state, 'number', Number(options.decimals), bindingBuilderUnitSuffix(options.unit)) : '---' };
        },
        previewNote(options) {
            const hours = Number(options.window) / 3600;
            const range = hours === 168 ? '7 days' : hours + (hours === 1 ? ' hour' : ' hours');
            return 'Last ' + range + '. Illustrative trend, not actual history. Preview only. Does not control the entity.';
        },
        renderPreview(cell, button) {
            const chart = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
            chart.setAttribute('class', 'pad-button-builder-sparkline');
            chart.setAttribute('viewBox', '0 0 280 70');
            chart.setAttribute('role', 'img');
            chart.setAttribute('aria-label', 'Illustrative trend, not actual history');
            const line = document.createElementNS('http://www.w3.org/2000/svg', 'polyline');
            line.setAttribute('points', '8,54 28,50 48,52 68,30 88,26 108,40 128,44 148,24 168,18 188,22 208,38 228,36 248,20 272,16');
            line.setAttribute('fill', 'none');
            line.setAttribute('stroke', button.widget_sparkline_line_color);
            line.setAttribute('stroke-width', String(button.widget_sparkline_line_width));
            const dot = document.createElementNS('http://www.w3.org/2000/svg', 'circle');
            dot.setAttribute('cx', '272');
            dot.setAttribute('cy', '16');
            dot.setAttribute('r', String(button.widget_sparkline_current_dot / 2));
            dot.setAttribute('fill', button.widget_sparkline_line_color);
            chart.appendChild(line);
            chart.appendChild(dot);
            cell.appendChild(chart);
        }
    },
    {
        id: 'toggle', label: 'Toggle an entity',
        description: 'Toggle a light or switch and show whether it is on or off.',
        note: 'Icon color follows the entity state. Unavailable data uses a pink-red icon.',
        preferred: true,
        available: detail => /^(light|switch)\./.test(detail.entity_id) && !!actionEditorCatalogEntry('ha_service'),
        fields: detail => [
            { name: 'icon', label: 'Material Symbol name', type: 'text', maxlength: 28, required: true,
                default: detail.entity_id.startsWith('light.') ? 'lightbulb' : 'toggle_on',
                link: { label: 'Browse Material Symbols', href: 'https://fonts.google.com/icons' } },
            { name: 'on', label: 'On color', type: 'color', default: '#FFC857',
                error: 'Choose valid on and off colors.' },
            { name: 'off', label: 'Off color', type: 'color', default: '#64748B',
                error: 'Choose valid on and off colors.' }
        ],
        build(detail, options) {
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
        preview(detail, options, state) {
            return { fg_color: state === 'on' ? options.on : state === 'off' ? options.off : '#EF6B73' };
        },
        previewStates: ['on', 'off', 'unavailable']
    }
];
