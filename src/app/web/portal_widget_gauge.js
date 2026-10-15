(() => {
    const fields = [];
    function field(name, key, kind, defaultValue, binding) {
        fields.push({ id: 'pad-edit-gauge-' + name, key, kind, default: defaultValue, binding });
    }
    function color(name, key, defaultKey) {
        field(name, key, 'color', () => PAD_FIRMWARE_DEFAULTS[defaultKey], {});
    }
    for (let source = 1; source <= 4; source++) {
        const suffix = source === 1 ? '' : '-' + source;
        const keySuffix = source === 1 ? '' : '_' + source;
        field('data-binding' + suffix, 'widget_data_binding' + keySuffix, 'text', '', { isWidgetBinding: true });
        field('start-label' + suffix, 'widget_gauge_start_label' + keySuffix, 'label', '', {});
        color('arc-color' + suffix, 'widget_arc_color' + keySuffix, 'widget_series_color' + keySuffix);
    }
    field('min', 'widget_gauge_min', 'bindable-number', 0, { isWidgetBinding: true });
    field('max', 'widget_gauge_max', 'bindable-number', 100, { isWidgetBinding: true });
    field('degrees', 'widget_gauge_degrees', 'number', 180);
    field('start-angle', 'widget_gauge_start_angle', 'number', 180);
    field('zero-centered', 'widget_gauge_zero_centered', 'checkbox', false);
    field('dual-binding-pair-1', 'widget_gauge_dual_binding_pair_1', 'checkbox', false);
    field('dual-binding-pair-2', 'widget_gauge_dual_binding_pair_2', 'checkbox', false);
    field('show-needle', 'widget_gauge_show_needle', 'checkbox', true);
    field('anim-ms', 'widget_anim_ms', 'number', 300);
    field('arc-width-pct', 'widget_gauge_arc_width_pct', 'number', 15);
    field('ticks', 'widget_gauge_ticks', 'number', 5);
    field('needle-width', 'widget_gauge_needle_width', 'number', 2);
    field('needle-cutoff', 'widget_gauge_needle_cutoff_pct', 'number', 0);
    field('tick-width', 'widget_gauge_tick_width', 'number', 1);
    field('marker-value', 'widget_gauge_marker_value', 'numeric-text', '', { isWidgetBinding: true });
    field('marker-zone-deg', 'widget_gauge_marker_zone_deg', 'number', 0);
    field('marker-tick-width', 'widget_gauge_marker_tick_width', 'number', 2);
    color('track-color', 'widget_gauge_track_color', 'widget_track_color');
    color('needle-color', 'widget_gauge_needle_color', 'widget_indicator_color');
    color('tick-color', 'widget_gauge_tick_color', 'widget_tick_color');
    color('marker-tick-color', 'widget_gauge_marker_tick_color', 'widget_indicator_color');
    color('marker-zone-color', 'widget_gauge_marker_zone_color', 'widget_zone_color');
    padRegisterWidgetEditor({
        type: 'gauge',
        sections: [{ id: 'pad-edit-gauge-section', tab: 'Content' }],
        actions: { tap: true, longPress: true },
        fields,
        disclosureSettings: [
            { group: 'pad-edit-gauge-layout', key: 'widget_track_outline_width', kind: 'number', default: 0 },
            { group: 'pad-edit-gauge-layout', key: 'widget_track_outline_color', kind: 'color', default: '#000000' }
        ],
        track: { host: 'pad-edit-gauge-track-settings', outline: true },
        icon: { fixedPosition: true, width: 0.4, height: 0.4 }
    });
})();