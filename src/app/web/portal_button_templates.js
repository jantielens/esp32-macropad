function padButtonBuilderDecimalField() {
    return { name: 'decimals', label: 'Decimal places', type: 'number', default: 1, min: 0, max: 6,
        error: 'Choose 0 to 6 decimal places.' };
}

function padButtonBuilderUnitField(unit) {
    return { name: 'unit', label: 'Unit', type: 'text', default: unit || '', maxlength: 60 };
}

function padButtonBuilderFormattingFields(candidate) {
    if (candidate.selection) return [];
    return [
        ...(candidate.formatModes.includes('number') ? [padButtonBuilderDecimalField()] : []),
        padButtonBuilderUnitField(candidate.unit)
    ];
}

function padButtonBuilderLabel(source, options, includeUnit = true) {
    if (source.candidate.selection) {
        const selection = source.candidate.selection;
        const display = { ...selection, unit: includeUnit ? selection.unit : '' };
        return bindingSourceToken(source.candidate, display);
    }
    const provider = padButtonBuilderProvider(source.candidate);
    if (provider.buildLabel) return provider.buildLabel(source.candidate, options, includeUnit);
    const modes = source.candidate.formatModes;
    const mode = modes.includes('number') ? 'number' : modes.includes('text') ? 'text' : 'raw';
    const suffix = includeUnit ? bindingBuilderUnitSuffix(options.unit) : '';
    return padButtonBuilderBinding(source, bindingBuilderFormat(mode, Number(options.decimals), suffix)) +
        (mode === 'raw' ? suffix : '');
}

function padButtonBuilderRangeFields(candidate) {
    const value = Number(candidate.sample);
    const percentage = candidate.unit?.trim() === '%';
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

function padButtonBuilderHistorySourceAvailable(source) {
    const provider = padButtonBuilderProvider(source.candidate);
    return source.candidate.capabilities.liveSampling &&
        (source.candidate.capabilities.numericWidget || !!provider.supportsHistory?.(source.candidate));
}

function padButtonBuilderHistorySampling({ window: range, interval }) {
    const windowSeconds = Number(range);
    const slots = Math.max(2, Math.min(1024, Math.ceil(windowSeconds / Number(interval))));
    return { windowSeconds, slots, interval: windowSeconds / slots };
}

function padButtonBuilderHistoryIcon(candidate) {
    if (candidate.providerId === 'ha' && !candidate.reference.path) {
        const deviceClass = candidate.reference.detail?.paths?.find(
            field => field.path === 'attributes.device_class' && !field.truncated)?.value;
        if (deviceClass === 'temperature') return 'thermometer';
        if (deviceClass === 'humidity') return 'humidity-percentage';
    }
    const unit = candidate.unit?.trim();
    if (unit === '°C' || unit === '°F') return 'thermometer';
    if (unit === '%') return 'percent';
    return 'numbers';
}

function padButtonBuilderIconHistoryDraft(draft) {
    return {
        ...draft,
        options: { ...draft.options, scale: 'shared' },
        sources: draft.sources.slice(0, 1).map(source => ({
            ...source, options: { ...source.options, color: draft.options.color }
        }))
    };
}

// Templates own their controls and configuration.
const padButtonBuilders = [
    {
        id: 'value', label: 'Show a value', aspectRatio: 1,
        description: 'Show the source name, current value, and unit.',
        note: 'The unit is display text only; changing it does not convert the value.',
        available: () => true,
        fields: ({ sources: [{ candidate }] }) => padButtonBuilderFormattingFields(candidate),
        build({ sources: [source], options }) {
            return {
                label_center: padButtonBuilderLabel(source, options, false),
                label_bottom: (source.candidate.selection?.unit ?? options.unit).trim()
            };
        }
    },
    {
        id: 'bar', label: 'Show a bar',
        aspectRatio: options => options.orientation === 'horizontal' ? 2 : 1,
        description: 'Show a numeric value as a single bar within a fixed range.',
        note: 'Check the suggested range before creating the button. The unit changes display text, not the value. Fine-tune styling in the pad editor.',
        available: ({ sources: [{ candidate }] }, runtime = deviceInfoCache) => candidate.capabilities.numericWidget &&
            !!runtime?.widget_catalog?.some(widget => widget.type === 'bar_chart'),
        fields({ sources: [{ candidate }] }) {
            return [
                ...padButtonBuilderRangeFields(candidate),
                { name: 'orientation', label: 'Orientation', type: 'select', default: 'vertical',
                    options: [['vertical', 'Vertical'], ['horizontal', 'Horizontal']],
                    error: 'Choose a valid orientation.' },
                ...padButtonBuilderFormattingFields(candidate)
            ];
        },
        build({ sources: [source], options }) {
            const { min, max } = padButtonBuilderRange(options);
            return {
                widget_type: 'bar_chart',
                widget_data_binding: padButtonBuilderBinding(source),
                widget_bar_min: min, widget_bar_max: max,
                widget_orientation: options.orientation,
                widget_bar_zero_centered: min < 0 && max > 0,
                label_bottom: padButtonBuilderLabel(source, options)
            };
        }
    },
    {
        id: 'gauge', label: 'Show a gauge', aspectRatio: 2,
        description: 'Show a numeric value as a single ring within a fixed range.',
        note: 'Check the suggested range before creating the button. The unit changes display text, not the value. Fine-tune styling in the pad editor.',
        available: ({ sources: [{ candidate }] }, runtime = deviceInfoCache) => candidate.capabilities.numericWidget &&
            !!runtime?.widget_catalog?.some(widget => widget.type === 'gauge'),
        fields: ({ sources: [{ candidate }] }) => [
            ...padButtonBuilderRangeFields(candidate),
            ...padButtonBuilderFormattingFields(candidate),
            { name: 'color', label: 'Arc color', type: 'color', default: PAD_FIRMWARE_DEFAULTS.widget_series_color,
                error: 'Choose a valid arc color.' }
        ],
        build({ sources: [source], options }) {
            const { min, max } = padButtonBuilderRange(options);
            return {
                widget_type: 'gauge',
                widget_data_binding: padButtonBuilderBinding(source),
                widget_gauge_min: min, widget_gauge_max: max,
                widget_gauge_degrees: 180, widget_gauge_start_angle: 180,
                widget_gauge_show_needle: false, widget_gauge_ticks: 0,
                widget_arc_color: options.color,
                label_bottom: padButtonBuilderLabel(source, options)
            };
        }
    },
    {
        id: 'history', label: 'Show values with history', aspectRatio: 3,
        description: 'Show a value and its trend, or compare up to three values.',
        note: 'Units change display text, not values. Automatic scale shares a scale only for matching known source units; otherwise each value has its own scale, so line heights are not directly comparable. Sampling is limited to 1024 points per line. Only eligible Home Assistant values load recorded history; other values are collected live while the device runs. Resize the button in the editor for more space.',
        maxSources: 3,
        available: ({ sources: [source] }, runtime = deviceInfoCache) => !!source && padButtonBuilderHistorySourceAvailable(source) &&
            !!runtime?.widget_catalog?.some(widget => widget.type === 'sparkline'),
        validateSource(source) {
            if (!padButtonBuilderHistorySourceAvailable(source)) {
                throw new Error('Choose a source with a numeric, non-truncated sample or a provider-supported history fallback.');
            }
        },
        fields: () => [
            { name: 'window', label: 'Time range', type: 'select', default: '86400',
                options: [['3600', '1 hour'], ['18000', '5 hours'], ['21600', '6 hours'], ['86400', '24 hours'], ['604800', '7 days'], ['2592000', '30 days']],
                error: 'Choose a valid time range.' },
            { name: 'interval', label: 'Requested sample interval (seconds)', type: 'number',
                default: 300, step: 'any', min: 0.1, max: 2592000, error: 'Enter a sample interval between 0.1 seconds and 30 days.' },
            { name: 'scale', label: 'Scale', type: 'select', default: 'auto',
                options: [['auto', 'Automatic (based on source units)'], ['shared', 'Shared scale'],
                    ['independent', 'Independent per value']], error: 'Choose a valid scale.' }
        ],
        sourceFields: ({ candidate }, index) => [
            ...padButtonBuilderFormattingFields(candidate),
            { name: 'color', label: 'Line color', type: 'color', default: ['#4CAF50', '#2196F3', '#9C27B0'][index],
                error: 'Choose a valid line color.' }
        ],
        sourceOptions({ candidate }, previous) {
            return previous?.candidate.sourceId === candidate.sourceId &&
                previous?.candidate.providerId === candidate.providerId ? {} : { unit: candidate.unit || '' };
        },
        validateOutput({ sources }, button) {
            sources.forEach((source, index) => {
                const suffix = index ? '_' + (index + 1) : '';
                padButtonBuilderValidateText(button['widget_sparkline_current_label' + suffix], 63);
                padButtonBuilderProvider(source.candidate).validateHistoryFields?.(
                    source.candidate, suffix, button, padButtonBuilderValidateText);
            });
        },
        build({ options, sources }) {
            const { windowSeconds, slots } = padButtonBuilderHistorySampling(options);
            const units = sources.map(source => (source.candidate.unit || '').trim());
            const knownUnitsMatch = units.every(Boolean) && new Set(units).size === 1;
            const button = {
                widget_type: 'sparkline',
                widget_sparkline_line_width: 2,
                widget_sparkline_current_dot: 5,
                widget_sparkline_smooth: 0,
                widget_sparkline_unified_scale: options.scale === 'shared' || (options.scale === 'auto' && knownUnitsMatch),
                widget_sparkline_window: windowSeconds,
                widget_sparkline_slots: slots
            };
            if (sources.length === 1) {
                button.label_center = padButtonBuilderLabel(sources[0], sources[0].options);
            } else {
                button.label_top = '';
                button.label_center = options.title.trim();
                button.label_center_style = 'align:left';
                button.widget_sparkline_label_width = 100;
            }
            sources.forEach((source, index) => {
                const suffix = index ? '_' + (index + 1) : '';
                button['widget_data_binding' + suffix] = padButtonBuilderBinding(source);
                button['widget_sparkline_line_color' + suffix] = source.options.color;
                if (sources.length > 1) {
                    button['widget_sparkline_current_label' + suffix] = padButtonBuilderLabel(source, source.options);
                }
                Object.assign(button, padButtonBuilderProvider(source.candidate)
                    .buildHistoryFields?.(source.candidate, suffix) || {});
            });
            return button;
        },
        samplingNote({ options }) {
            const sampling = padButtonBuilderHistorySampling(options);
            const slots = sampling.slots;
            const interval = Number(sampling.interval.toFixed(2));
            return 'Actual sample interval: ' + interval + ' seconds (' + slots + ' points per line).';
        }
    },
    {
        id: 'icon_history', label: 'Show a value with icon and history', aspectRatio: 3,
        description: 'Show one value with an icon and a filled trend.',
        note: 'The icon is suggested from the source metadata. Change it or fine-tune the colors in the pad editor. Resize the button there for more space.',
        available: (draft, runtime) => padButtonBuilders.find(item => item.id === 'history').available(draft, runtime),
        validateSource: source => padButtonBuilders.find(item => item.id === 'history').validateSource(source),
        fields: () => [
            ...padButtonBuilders.find(item => item.id === 'history').fields().slice(0, 2),
            { name: 'color', label: 'Accent color', type: 'color', default: '#4CAF50',
                error: 'Choose a valid accent color.' }
        ],
        sourceFields: ({ candidate }) => padButtonBuilderFormattingFields(candidate),
        sourceOptions: (source, previous) =>
            padButtonBuilders.find(item => item.id === 'history').sourceOptions(source, previous),
        validateOutput: (draft, button) =>
            padButtonBuilders.find(item => item.id === 'history').validateOutput(padButtonBuilderIconHistoryDraft(draft), button),
        build(draft) {
            const history = padButtonBuilders.find(item => item.id === 'history');
            return {
                ...history.build(padButtonBuilderIconHistoryDraft(draft)),
                label_top_style: 'align:left', label_center_style: 'align:left',
                group_size_pct: 60, icon_position: 'left', icon_scale_pct: 75,
                icon: { catalog_id: 'material-symbols:' + padButtonBuilderHistoryIcon(draft.sources[0].candidate), kind: 'mono' },
                icon_color: draft.options.color, icon_badge_padding: 4,
                widget_sparkline_line_width: 3, widget_sparkline_fill_opacity: 50
            };
        },
        samplingNote: draft => padButtonBuilders.find(item => item.id === 'history').samplingNote(draft)
    },
    {
        id: 'toggle', label: 'Toggle an entity', aspectRatio: 1.5,
        description: 'Toggle a light or switch and show whether it is on or off.',
        note: 'Icon color follows the entity state. Unavailable data uses a pink-red icon.',
        preferred: true,
        available: ({ sources: [source] }) => !!source?.candidate.capabilities.control &&
            !!padButtonBuilderProvider(source.candidate).controlFields?.(source.candidate).length &&
            !!actionEditorCatalogEntry(padButtonBuilderProvider(source.candidate).controlActionType),
        fields: ({ sources: [source] }) =>
            padButtonBuilderProvider(source.candidate).controlFields(source.candidate),
        build({ sources: [source], options }) {
            return padButtonBuilderProvider(source.candidate).buildControl(source.candidate, options);
        }
    },
    {
        id: 'toggle_widget', label: 'Show a toggle switch', aspectRatio: 2,
        description: 'Control a light or switch with an on/off switch in the button.',
        note: 'The switch follows the entity state and sends an explicit turn-on or turn-off action.',
        available: ({ sources: [source] }, runtime = deviceInfoCache) => {
            const provider = source && padButtonBuilderProvider(source.candidate);
            return !!source?.candidate.capabilities.control && !!provider?.buildToggleWidget &&
                !!actionEditorCatalogEntry(provider.controlActionType) &&
                !!runtime?.widget_catalog?.some(widget => widget.type === 'toggle');
        },
        fields: () => [],
        build({ sources: [source] }) {
            return padButtonBuilderProvider(source.candidate).buildToggleWidget(source.candidate);
        }
    }
];
