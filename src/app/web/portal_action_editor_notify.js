['notify', 'confirm'].forEach(function(type) {
    var confirmation = type === 'confirm';
    _actionEditorExtensions.push({
        type: type,
        bindingSuffixes: ['-' + type + '-message'],
        groups: function(prefix) {
            var id = prefix + '-' + type;
            var html = '<div id="' + id + '-group" style="display:none;">';
            html += '<div class="form-group"><label for="' + id + '-message">Message <span class="fx-hint" onclick="showBindingHelp()">fx</span></label>';
            html += '<textarea class="form-control form-control-sm" id="' + id + '-message" maxlength="191" rows="3"></textarea>';
            html += '<small>Up to 191 UTF-8 bytes. Line breaks and bindings supported; resolved once when this action runs.</small></div>';
            if (confirmation) {
                ['approve', 'cancel'].forEach(function(button) {
                    html += '<div class="form-group"><label for="' + id + '-' + button + '">' + (button === 'approve' ? 'Approve' : 'Cancel') + ' text</label>';
                    html += '<input class="form-control form-control-sm" id="' + id + '-' + button + '" maxlength="27" value="' + (button === 'approve' ? 'Continue' : 'Cancel') + '">';
                    html += '<small>Literal caption, up to 27 UTF-8 bytes.</small></div>';
                });
            }
            html += '<div class="form-group"><label for="' + id + '-seconds">' + (confirmation ? 'Timeout' : 'Display duration') + ' (seconds)</label>';
            html += '<input type="number" class="form-control form-control-sm" id="' + id + '-seconds" min="1" max="300" step="1" value="' + (confirmation ? '10' : '3') + '">';
            html += '<label><input type="checkbox" id="' + id + '-persistent" onchange="document.getElementById(\'' + id + '-seconds\').disabled=this.checked"> ' + (confirmation ? 'No timeout' : 'Until dismissed') + '</label></div>';
            html += '<small>Appearance comes from <a href="#dialog-appearance">Dialog appearance</a>. ' +
                (confirmation ? 'Approval resumes the remaining actions. Cancel, timeout, replacement, or failure stops them. Later actions resolve their own bindings.' : 'Blocks pad input until dismissed, but does not pause the action sequence.') + '</small></div>';
            return html;
        },
        typeChanged: function(prefix, selected) {
            var group = document.getElementById(prefix + '-' + type + '-group');
            if (group) group.style.display = selected === type ? '' : 'none';
        },
        load: function(prefix, action) {
            var id = prefix + '-' + type;
            var message = document.getElementById(id + '-message');
            if (message) message.value = action.message || '';
            var timing = action[confirmation ? 'timeout_ms' : 'duration_ms'];
            var seconds = document.getElementById(id + '-seconds');
            var persistent = document.getElementById(id + '-persistent');
            if (seconds) {
                seconds.value = timing ? timing / 1000 : confirmation ? 10 : 3;
                seconds.disabled = timing === 0;
            }
            if (persistent) persistent.checked = timing === 0;
            if (confirmation) {
                ['approve', 'cancel'].forEach(function(button) {
                    var field = document.getElementById(id + '-' + button);
                    if (field) field.value = action[button + '_text'] || (button === 'approve' ? 'Continue' : 'Cancel');
                });
            }
        },
        build: function(prefix, selected) {
            if (selected !== type) return null;
            var id = prefix + '-' + type;
            var action = {message: document.getElementById(id + '-message').value};
            if (!action.message.trim() || new TextEncoder().encode(action.message).length > 191)
                throw Object.assign(new Error('Dialog message is required and must fit 191 UTF-8 bytes.'), {fieldIds: [id + '-message']});
            var seconds = Number(document.getElementById(id + '-seconds').value);
            var persistent = document.getElementById(id + '-persistent').checked;
            if (!persistent && (!Number.isInteger(seconds) || seconds < 1 || seconds > 300))
                throw Object.assign(new Error('Dialog timing must be 1-300 whole seconds.'), {fieldIds: [id + '-seconds']});
            action[confirmation ? 'timeout_ms' : 'duration_ms'] = persistent ? 0 : seconds * 1000;
            if (confirmation) {
                ['approve', 'cancel'].forEach(function(button) {
                    var text = document.getElementById(id + '-' + button).value;
                    if (!text.trim() || new TextEncoder().encode(text).length > 27)
                        throw Object.assign(new Error('Confirmation captions are required and must fit 27 UTF-8 bytes.'), {fieldIds: [id + '-' + button]});
                    action[button + '_text'] = text;
                });
            }
            return action;
        }
    });
});
