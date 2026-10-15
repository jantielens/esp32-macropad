function actionEditorScreenTargetChanged(prefix) {
    var select = document.getElementById(prefix + '-target');
    if (select) select.removeAttribute('data-pending-value');
    actionEditorScreenRefresh(prefix);
}

function actionEditorScreenRefresh(prefix) {
    var select = document.getElementById(prefix + '-target');
    var group = document.getElementById(prefix + '-target-binding-group');
    var bindingMode = select && select.value === '__binding__';
    if (group) group.style.display = bindingMode ? '' : 'none';
    var input = document.getElementById(prefix + '-target-binding');
    if (input) input.disabled = !bindingMode;
    if (!bindingMode && input && typeof bindingClearError === 'function') bindingClearError(input);
}

function actionEditorPopulateScreens(prefixes, screens) {
    if (!screens) return;
    var pads = screens.filter(function(screen) { return /^pad_\d+$/.test(screen.id); });
    var builtins = screens.filter(function(screen) { return !/^pad_\d+$/.test(screen.id); });
    pads.sort(function(first, second) { return Number(first.id.slice(4)) - Number(second.id.slice(4)); });
    prefixes.forEach(function(prefix) {
        var sel = document.getElementById(prefix + '-target');
        if (!sel) return;
        var selected = sel.getAttribute('data-pending-value') || sel.value;
        while (sel.options.length > 1) sel.remove(1);
        pads.forEach(function(screen) {
            var opt = document.createElement('option');
            var label = 'Pad ' + (Number(screen.id.slice(4)) + 1);
            opt.value = screen.id;
            opt.textContent = !screen.name || screen.name === label ? label :
                screen.name.indexOf(label + ': ') === 0 ? screen.name : label + ': ' + screen.name;
            sel.appendChild(opt);
        });
        var binding = document.createElement('option');
        binding.value = '__binding__';
        binding.textContent = 'Binding value';
        sel.appendChild(binding);
        builtins.forEach(function(s) {
            var opt = document.createElement('option');
            opt.value = s.id;
            opt.textContent = s.name;
            sel.appendChild(opt);
        });
        sel.value = selected;
        sel.removeAttribute('data-pending-value');
        sel.setAttribute('data-screens-loaded', '1');
        actionEditorScreenRefresh(prefix);
    });
}

_actionEditorExtensions.push({
    type: 'screen',
    bindingSuffixes: ['-target-binding'],
    groups: function(prefix, opts) {
        var h = '';
        h += '<div id="' + prefix + '-screen-group" style="display:none;">';
        h += '<div class="form-group">';
        h += '<label class="form-label" for="' + prefix + '-target">Target Screen</label>';
        h += '<select class="form-select form-select-sm" id="' + prefix + '-target" oninput="actionEditorScreenTargetChanged(\'' + prefix + '\')" onchange="actionEditorScreenTargetChanged(\'' + prefix + '\')"><option value="">(none)</option><option value="__binding__">Binding value</option></select>';
        h += '</div><div class="form-group" id="' + prefix + '-target-binding-group" style="display:none;">';
        h += '<label class="form-label" for="' + prefix + '-target-binding">Binding value <span class="fx-hint" onclick="showBindingHelp()">fx</span></label>';
        h += '<input type="text" class="form-control form-control-sm binding-mono" id="' + prefix + '-target-binding" placeholder="[list:pads.selected]">';
        h += '</div></div>';
        return h;
    },
    typeChanged: function(prefix, type) {
        var screenGrp = document.getElementById(prefix + '-screen-group');
        if (screenGrp) screenGrp.style.display = (type === 'screen') ? '' : 'none';
        actionEditorScreenRefresh(prefix);
    },
    load: function(prefix, action) {
        var el = document.getElementById(prefix + '-target');
        var input = document.getElementById(prefix + '-target-binding');
        var target = action.target || '';
        var bindingMode = target.indexOf('[') !== -1;
        if (input) input.value = bindingMode ? target : '';
        if (el) {
            el.removeAttribute('data-pending-value');
            el.value = bindingMode ? '__binding__' : target;
            if (!bindingMode && target && el.value !== target && !el.hasAttribute('data-screens-loaded')) {
                el.setAttribute('data-pending-value', target);
            }
        }
        actionEditorScreenRefresh(prefix);
    },
    build: function(prefix, type) {
        if (type !== 'screen') return null;
        var select = document.getElementById(prefix + '-target');
        var bindingMode = select && select.value === '__binding__';
        var input = bindingMode ? document.getElementById(prefix + '-target-binding') : select;
        var target = input ? input.value.trim() : '';
        var error = '';
        if (bindingMode) {
            var result = validateBinding(target);
            if (!result.valid) error = result.errors[0].message;
            else if (!bindingTokenize(target).length) error = 'Enter a screen target binding.';
        }
        if (new TextEncoder().encode(target).length > 31) error = 'Screen target must be at most 31 UTF-8 bytes.';
        if (error) {
            if (input && typeof bindingShowError === 'function') bindingShowError(input, error);
            if (typeof padWorkspace === 'undefined' || !padWorkspace) {
                if (input) input.focus();
                if (typeof showMessage === 'function') showMessage(error, 'error');
            }
            throw Object.assign(new Error(error), { fieldIds: [input.id] });
        }
        if (input && typeof bindingClearError === 'function') bindingClearError(input);
        return { target: target };
    },
    wireFragment: function(prefixes, info) {
        actionEditorPopulateScreens(prefixes, info && info.available_screens);
    }
});
