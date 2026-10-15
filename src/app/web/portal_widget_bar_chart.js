(() => {
    const fields = [];
    const patterns = ['widget_bar_pattern', 'widget_bar_pattern_2', 'widget_bar_pattern_3', 'widget_bar_pattern_4', 'widget_bar_bg_pattern'];
    function field(id, key, kind, defaultValue, binding) {
        const descriptor = { id: 'pad-edit-' + id, key, kind, default: defaultValue, binding };
        fields.push(descriptor);
        return descriptor;
    }
    function color(name, key, defaultKey) {
        return field('widget-bar-' + name, key, 'color', () => PAD_FIRMWARE_DEFAULTS[defaultKey], {});
    }
    for (let source = 1; source <= 4; source++) {
        const suffix = source === 1 ? '' : '-' + source;
        const keySuffix = source === 1 ? '' : '_' + source;
        field(source === 1 ? 'widget-data-binding' : 'widget-bar-data-binding' + suffix,
            'widget_data_binding' + keySuffix, 'text', '', { isWidgetBinding: true });
        field('widget-bar-label' + suffix, 'widget_bar_label' + keySuffix, 'text', '', {});
        const sourceColor = color('color' + suffix, 'widget_bar_color' + keySuffix, 'widget_series_color' + keySuffix);
        sourceColor.preview = resolved => padWidgetSetGroupColor('pad-edit-bar-source-' + source, sourceColor, resolved);
    }
    field('widget-bar-min', 'widget_bar_min', 'bindable-number', 0, { isWidgetBinding: true });
    field('widget-bar-max', 'widget_bar_max', 'bindable-number', 3, { isWidgetBinding: true });
    field('widget-orientation', 'widget_orientation', 'text', 'vertical');
    field('widget-bar-zero-centered', 'widget_bar_zero_centered', 'checkbox', false);
    field('widget-bar-dual-binding-pair-1', 'widget_bar_dual_binding_pair_1', 'checkbox', false);
    field('widget-bar-dual-binding-pair-2', 'widget_bar_dual_binding_pair_2', 'checkbox', false);
    for (const [name, value] of [['label-size', 0], ['width-pct', 100], ['ticks', 0], ['tick-width', 1],
        ['marker-zone-pct', 0], ['marker-width', 2]]) {
        field('widget-bar-' + name, 'widget_bar_' + name.replaceAll('-', '_'), 'number', value);
    }
    field('widget-bar-anim-ms', 'widget_anim_ms', 'number', 300);
    field('widget-bar-marker-value', 'widget_bar_marker_value', 'numeric-text', '', { isWidgetBinding: true });
    color('bg-color', 'widget_bar_bg_color', 'widget_track_color');
    color('tick-color', 'widget_bar_tick_color', 'widget_tick_color');
    color('marker-color', 'widget_bar_marker_color', 'widget_indicator_color');
    color('marker-zone-color', 'widget_bar_marker_zone_color', 'widget_zone_color');
    field('track-radius', 'widget_bar_track_radius', 'number', 4);
    padRegisterWidgetEditor({
        type: 'bar_chart',
        sections: [{ id: 'pad-edit-bar-chart-section', tab: 'Content' }],
        actions: { tap: true, longPress: true },
        fields,
        disclosureSettings: [
            ...patterns.flatMap((key, index) => {
                const group = index < 4 ? 'pad-edit-bar-source-' + (index + 1) : 'pad-edit-bar-layout';
                return [{ group, key, kind: 'text', default: 'none' },
                    { group, key: key + '_color', kind: 'color', default: '#000000' }];
            }),
            { group: 'pad-edit-bar-layout', key: 'widget_track_outline_width', kind: 'number', default: 0 },
            { group: 'pad-edit-bar-layout', key: 'widget_track_outline_color', kind: 'color', default: '#000000' },
            { group: 'pad-edit-bar-layout', key: 'widget_bar_track_radius', kind: 'number', default: 4 }
        ],
        previewFields: fields.filter(field => field.kind === 'color' && field.id !== 'pad-edit-widget-bar-tick-color' &&
            field.id !== 'pad-edit-widget-bar-marker-color' && field.id !== 'pad-edit-widget-bar-marker-zone-color').map(field => ({
            id: field.id + '-pattern-color',
            field: field.key.replace('_color', '_pattern_color'),
            kind: 'color'
        })),
        patterns,
        track: { host: 'pad-edit-bar-track-settings', outline: true, radius: true },
        icon: { fixedPosition: true, width: 1, height: 0.5 }
    });
})();