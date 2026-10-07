function actionEditorAlarmFieldActive(prefix, name) {
    var command = document.getElementById(prefix + '-generic-alarm-alarm_command');
    var value = command ? command.value : '';
    if (name === 'alarm_value') return value === 'set_time' || value === 'adjust_minutes';
    if (name === 'alarm_day') return ['weekday_enable', 'weekday_disable', 'weekday_toggle'].indexOf(value) >= 0;
    return true;
}

function actionEditorAlarmChanged(prefix) {
    ['alarm_value', 'alarm_day'].forEach(function(name) {
        var group = document.getElementById(prefix + '-generic-alarm-' + name + '-field');
        if (group) group.style.display = actionEditorAlarmFieldActive(prefix, name) ? '' : 'none';
    });
    var command = document.getElementById(prefix + '-generic-alarm-alarm_command');
    var label = document.getElementById(prefix + '-generic-alarm-alarm_value-label');
    if (label) label.textContent = command && command.value === 'set_time' ? 'Time (minutes since midnight)' : 'Minutes';
}

(function () {
    if (typeof _actionEditorExtensions === 'undefined') return;

    _actionEditorExtensions.push({
        type: 'alarm',
        groups: function(prefix) {
            var entry = actionEditorCatalogEntry('alarm');
            if (!entry || !entry.editor_fields) return '';
            var html = '<div id="' + prefix + '-alarm-group" style="display:none;">';
            entry.editor_fields.forEach(function(field) {
                var id = prefix + '-generic-alarm-' + field.name;
                var conditional = field.name === 'alarm_value' || field.name === 'alarm_day';
                var label = field.name === 'alarm_day' ? 'Weekday' : field.name === 'alarm_value' ? 'Minutes' : field.label;
                html += '<div class="form-group" id="' + id + '-field"' + (conditional ? ' style="display:none;"' : '') + '>';
                html += '<label class="form-label" for="' + id + '"><span id="' + id + '-label">' + label + '</span>';
                if (field.bindable) html += ' <span class="fx-hint" onclick="showBindingHelp()">fx</span>';
                html += '</label>';
                if (field.type === 'select') {
                    html += '<select class="form-select form-select-sm" id="' + id + '"' + (field.name === 'alarm_command' ? ' onchange="actionEditorAlarmChanged(\'' + prefix + '\')"' : '') + '>';
                    if (field.command_options) html += actionEditorCommandOptionsHTML('alarm', field.default);
                    else (field.options || []).forEach(function(option) {
                        html += '<option value="' + option.id + '"' + (String(option.id) === String(field.default) ? ' selected' : '') + '>' + option.label + '</option>';
                    });
                    html += '</select>';
                } else html += '<input type="text" class="form-control form-control-sm" id="' + id + '">';
                html += '</div>';
            });
            return html + '</div>';
        },
        typeChanged: function(prefix, type) {
            var group = document.getElementById(prefix + '-alarm-group');
            if (group) group.style.display = type === 'alarm' ? '' : 'none';
            if (type !== 'alarm') return;
            actionEditorAlarmChanged(prefix);
            var input = document.getElementById(prefix + '-generic-alarm-alarm_value');
            if (input && !input.dataset.bcBind && typeof bindingAttachValidation === 'function') {
                input.dataset.bcBind = '1';
                bindingAttachValidation(input);
            }
        },
        load: function(prefix, action) {
            if (action.type !== 'alarm') return;
            var entry = actionEditorCatalogEntry('alarm');
            if (!entry || !entry.editor_fields) return;
            entry.editor_fields.forEach(function(field) {
                var input = document.getElementById(prefix + '-generic-alarm-' + field.name);
                if (input) input.value = action[field.name] === undefined ? (field.default === undefined ? '' : field.default) : action[field.name];
            });
            actionEditorAlarmChanged(prefix);
        },
        build: function(prefix, type) {
            if (type !== 'alarm') return null;
            var entry = actionEditorCatalogEntry('alarm');
            var action = {};
            if (!entry || !entry.editor_fields) return action;
            entry.editor_fields.forEach(function(field) {
                if (!actionEditorAlarmFieldActive(prefix, field.name)) return;
                var input = document.getElementById(prefix + '-generic-alarm-' + field.name);
                if (input && input.value !== '') action[field.name] = field.numeric ? Number(input.value) : input.value.trim();
            });
            return action;
        }
    });
})();