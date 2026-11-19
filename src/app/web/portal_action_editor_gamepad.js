function actionEditorGamepadButtonCount() {
    var operation = actionEditorOperation('gamepad.button.down');
    var argument = operation && operation.arguments.find(function(item) { return item.name === 'button'; });
    return argument && Number.isInteger(argument.max) && argument.max > 0 ? argument.max : 0;
}

function actionEditorGamepadHTML(prefix) {
    var html = '<div id="' + prefix + '-gamepad-group">';
    var fields = [
        ['control', 'Control', [['button', 'Button'], ['hat', 'Hat direction'], ['trigger', 'Trigger']]],
        ['button', 'Button', Array.from({ length: actionEditorGamepadButtonCount() }, function(_, index) { return [String(index + 1), String(index + 1)]; })],
        ['direction', 'Direction', [['up', 'Up'], ['down', 'Down'], ['left', 'Left'], ['right', 'Right']]],
        ['trigger', 'Trigger', [['left', 'Left'], ['right', 'Right']]]
    ];
    fields.forEach(function(field) {
        var id = prefix + '-gamepad-' + field[0];
        html += '<div class="form-group" id="' + id + '-field"><label class="form-label" for="' + id + '">' + field[1] + '</label>';
        html += '<select class="form-select form-select-sm" id="' + id + '"' +
            (field[0] === 'control' ? ' onchange="actionEditorGamepadChanged(\'' + prefix + '\')"' : '') + '>';
        field[2].forEach(function(option) { html += '<option value="' + option[0] + '">' + option[1] + '</option>'; });
        html += '</select></div>';
    });
    return html + '</div>';
}

function actionEditorGamepadChanged(prefix) {
    var control = document.getElementById(prefix + '-gamepad-control');
    if (!control) return;
    ['button', 'direction', 'trigger'].forEach(function(field) {
        var group = document.getElementById(prefix + '-gamepad-' + field + '-field');
        if (group) group.style.display = (field === 'direction' ? 'hat' : field) === control.value ? '' : 'none';
    });
}

function actionEditorLoadGamepad(prefix, action) {
    var defaults = { control: 'button', button: 1, direction: 'up', trigger: 'left' };
    Object.keys(defaults).forEach(function(field) {
        var input = document.getElementById(prefix + '-gamepad-' + field);
        if (input) input.value = action[field] === undefined ? defaults[field] : action[field];
    });
    actionEditorGamepadChanged(prefix);
}

function actionEditorBuildGamepad(prefix) {
    function value(field, fallback) {
        var input = document.getElementById(prefix + '-gamepad-' + field);
        return input && input.value ? input.value : fallback;
    }
    var control = value('control', 'button');
    var args = {};
    if (control === 'button') {
        args.button = Number(value('button', '1'));
        var buttonCount = actionEditorGamepadButtonCount();
        if (!buttonCount) throw Object.assign(new Error('Gamepad button controls are unavailable'), { fieldIds: [prefix + '-gamepad-control'] });
        if (!Number.isInteger(args.button) || args.button < 1 || args.button > buttonCount) throw Object.assign(new Error('Gamepad button must be 1-' + buttonCount), { fieldIds: [prefix + '-gamepad-button'] });
    } else if (control === 'hat') {
        args.direction = value('direction', 'up');
        if (['up', 'down', 'left', 'right'].indexOf(args.direction) < 0) throw Object.assign(new Error('Invalid Gamepad hat direction'), { fieldIds: [prefix + '-gamepad-direction'] });
    } else if (control === 'trigger') {
        args.trigger = value('trigger', 'left');
        if (['left', 'right'].indexOf(args.trigger) < 0) throw Object.assign(new Error('Invalid Gamepad trigger'), { fieldIds: [prefix + '-gamepad-trigger'] });
    } else throw Object.assign(new Error('Invalid Gamepad control'), { fieldIds: [prefix + '-gamepad-control'] });
    return { op: 'gamepad.' + control + '.down', args: args };
}
