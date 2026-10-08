function actionEditorGamepadButtonCount() {
    var entry = actionEditorCatalogEntry('gamepad');
    return entry && Number.isInteger(entry.button_count) && entry.button_count > 0 ? entry.button_count : 0;
}

function actionEditorGamepadHTML(prefix, held) {
    var html = '<div id="' + prefix + '-gamepad-group" style="display:' + (held ? '' : 'none') + ';">';
    var fields = [
        ['control', 'Control', [['button', 'Button'], ['hat', 'Hat direction'], ['trigger', 'Trigger']]],
        ['button', 'Button', Array.from({ length: actionEditorGamepadButtonCount() }, function(_, index) { return [String(index + 1), String(index + 1)]; })],
        ['direction', 'Direction', [['up', 'Up'], ['down', 'Down'], ['left', 'Left'], ['right', 'Right']]],
        ['trigger', 'Trigger', [['left', 'Left'], ['right', 'Right']]]
    ];
    if (!held) fields.push(['operation', 'Operation', [['tap', 'Tap'], ['down', 'Down'], ['up', 'Up']]]);
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
    var defaults = { control: 'button', button: 1, direction: 'up', trigger: 'left', operation: 'tap' };
    Object.keys(defaults).forEach(function(field) {
        var input = document.getElementById(prefix + '-gamepad-' + field);
        if (input) input.value = action[field] === undefined ? defaults[field] : action[field];
    });
    actionEditorGamepadChanged(prefix);
}

function actionEditorBuildGamepad(prefix, held) {
    function value(field, fallback) {
        var input = document.getElementById(prefix + '-gamepad-' + field);
        return input && input.value ? input.value : fallback;
    }
    var action = { type: 'gamepad', control: value('control', 'button'), operation: held ? 'down' : value('operation', 'tap') };
    if (action.control === 'button') {
        action.button = Number(value('button', '1'));
        var buttonCount = actionEditorGamepadButtonCount();
        if (!buttonCount) throw new Error('Gamepad button controls are unavailable');
        if (!Number.isInteger(action.button) || action.button < 1 || action.button > buttonCount) throw new Error('Gamepad button must be 1-' + buttonCount);
    } else if (action.control === 'hat') {
        action.direction = value('direction', 'up');
        if (['up', 'down', 'left', 'right'].indexOf(action.direction) < 0) throw new Error('Invalid Gamepad hat direction');
    } else if (action.control === 'trigger') {
        action.trigger = value('trigger', 'left');
        if (['left', 'right'].indexOf(action.trigger) < 0) throw new Error('Invalid Gamepad trigger');
    } else throw new Error('Invalid Gamepad control');
    if (['tap', 'down', 'up'].indexOf(action.operation) < 0) throw new Error('Invalid Gamepad operation');
    return action;
}

_actionEditorExtensions.push({
    type: 'gamepad',
    groups: function(prefix, opts) {
        var h = '';
        h += actionEditorGamepadHTML(prefix, false);
        return h;
    },
    typeChanged: function(prefix, type) {
        var group = document.getElementById(prefix + '-gamepad-group');
        if (group) group.style.display = type === 'gamepad' ? '' : 'none';
        if (type === 'gamepad') actionEditorGamepadChanged(prefix);
    },
    load: function(prefix, action) {
        var el;
        actionEditorLoadGamepad(prefix, action.type === 'gamepad' ? action : {});
    },
    build: function(prefix, type) {
        if (type !== 'gamepad') return null;
        var act = {};
    act = actionEditorBuildGamepad(prefix, false);
        return act;
    }
});
