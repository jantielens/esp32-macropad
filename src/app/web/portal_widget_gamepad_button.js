var padGamepadHeldInvalid = false;

padRegisterWidgetEditor({
    type: 'gamepad_button',
    available: info => !!(info && info.has_touch === true && (info.has_usb_hid === true || info.has_ble_hid === true)),
    sections: [{ id: 'pad-edit-gamepad-button-section', tab: 'Content' }],
    fields: [],
    inputNavigation: true,
    actions: {
        tap: false,
        longPress: false,
        buildTap() {
            if (padGamepadHeldInvalid) {
                throw Object.assign(new Error('Choose a literal Gamepad held control to replace the unsupported action'),
                    { fieldIds: ['pad-edit-gamepad-hold-gamepad-control'] });
            }
            return [actionEditorBuildGamepad('pad-edit-gamepad-hold')];
        }
    },
    init() {
        document.getElementById('pad-edit-gamepad-button-fields').innerHTML = actionEditorGamepadHTML('pad-edit-gamepad-hold');
        ['control', 'button', 'direction', 'trigger'].forEach(field => {
            document.getElementById('pad-edit-gamepad-hold-gamepad-' + field)
                .addEventListener('change', () => { padGamepadHeldInvalid = false; });
        });
    },
    load(button) {
        const actions = Array.isArray(button.actions) ? button.actions : [];
        const action = actions[0];
        let selection = {};
        padGamepadHeldInvalid = (button.actions !== undefined && !Array.isArray(button.actions)) ||
            actions.length > 1 || (actions.length === 1 && (!action || typeof action !== 'object'));
        if (action) {
            const controls = {
                'gamepad.button.down': ['button', 'button'],
                'gamepad.hat.down': ['hat', 'direction'],
                'gamepad.trigger.down': ['trigger', 'trigger']
            };
            const control = controls[action.op];
            const args = action.args;
            const value = control && args && args[control[1]];
            const validValue = control && (control[0] === 'button'
                ? Number.isInteger(value) && value >= 1 && value <= actionEditorGamepadButtonCount()
                : (control[0] === 'hat' ? ['up', 'down', 'left', 'right'] : ['left', 'right']).includes(value));
            if (!control || !args || typeof args !== 'object' || Array.isArray(args) ||
                Object.keys(action).length !== 2 || Object.keys(args).length !== 1 || !validValue) {
                padGamepadHeldInvalid = true;
            } else {
                selection = { control: control[0], [control[1]]: value };
            }
        }
        actionEditorLoadGamepad('pad-edit-gamepad-hold', selection);
    }
});