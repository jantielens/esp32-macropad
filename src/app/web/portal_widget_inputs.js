(() => {
    const available = info => !!(info && info.has_touch === true && info.has_usb_hid === true);
    function fields(prefix, keyPrefix, definitions) {
        return definitions.map(([name, kind, value]) => ({
            id: 'pad-edit-' + prefix + '-' + name,
            key: 'widget_' + keyPrefix + '_' + name.replaceAll('-', '_'), kind, default: value
        }));
    }
    padRegisterWidgetEditor({
        type: 'mousepad', available,
        sections: [{ id: 'pad-edit-mousepad-section', tab: 'Content' }],
        inputNavigation: true, actions: { tap: false, longPress: false },
        fields: fields('mousepad', 'mousepad', [
            ['sensitivity', 'number', 1], ['acceleration', 'number', 0],
            ['movement-threshold', 'number', 3], ['inertia', 'number', 0],
            ['reverse', 'checkbox', false], ['buttons', 'checkbox', false],
            ['back', 'checkbox', false], ['back-size', 'number', 44]
        ])
    });
    padRegisterWidgetEditor({
        type: 'scrollpad', available,
        sections: [{ id: 'pad-edit-scrollpad-section', tab: 'Content' }],
        inputNavigation: true, actions: { tap: false, longPress: false },
        fields: fields('scrollpad', 'scrollpad', [
            ['axis', 'text', 'vertical'], ['sensitivity', 'number', 1],
            ['inertia', 'number', 0], ['reverse', 'checkbox', false]
        ])
    });
    padRegisterWidgetEditor({
        type: 'gamepad_stick', available,
        sections: [{ id: 'pad-edit-gamepad-stick-section', tab: 'Content' }],
        inputNavigation: true, actions: { tap: false, longPress: false },
        fields: fields('gamepad', 'gamepad', [
            ['stick', 'text', 'left'], ['center', 'text', 'fixed'],
            ['invert-x', 'checkbox', false], ['invert-y', 'checkbox', false]
        ]),
        load(button) {
            document.getElementById('pad-edit-gamepad-dead-zone').value =
                (button.widget_gamepad_dead_zone === undefined ? 0.1 : button.widget_gamepad_dead_zone) * 100;
        },
        build(button) {
            const input = document.getElementById('pad-edit-gamepad-dead-zone');
            padWidgetValidateField(input, { kind: 'number' });
            button.widget_gamepad_dead_zone = (input.value.trim() === '' ? 10 : Number(input.value)) / 100;
        }
    });
})();