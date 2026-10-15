padRegisterWidgetEditor({
    type: 'gamepad_button',
    optionId: 'pad-edit-gamepad_button-widget-option',
    available: info => !!(info && info.has_touch === true && info.has_usb_hid === true),
    sections: [{ id: 'pad-edit-gamepad-button-section', tab: 'Content' }],
    fields: [],
    inputNavigation: true,
    actions: {
        tap: false,
        longPress: false,
        buildTap: () => [actionEditorBuildGamepad('pad-edit-gamepad-hold', true)]
    },
    init() {
        document.getElementById('pad-edit-gamepad-button-fields').innerHTML = actionEditorGamepadHTML('pad-edit-gamepad-hold', true);
    },
    load(button) {
        const actions = button.actions || [];
        actionEditorLoadGamepad('pad-edit-gamepad-hold', actions[0] || {});
    }
});