function listInjectSyntheticScreenOption(prefix) {
    var sel = document.getElementById(prefix + '-target');
    if (!sel || sel.tagName !== 'SELECT') return;
    // Capture the current value so we can restore it after removing the old
    // synthetic option (which may itself be the currently selected option,
    // since removing a selected <option> resets the dropdown to the first item).
    var prevValue = sel.value;
    // Helper: restore the previous value if it still maps to an existing option.
    var restorePrev = function() {
        if (prevValue && sel.value !== prevValue) sel.value = prevValue;
    };
    // Remove any previously injected synthetic option
    var existing = sel.querySelector('option[data-synthetic]');
    if (existing) existing.remove();
    // Only inject for list widget with a provider ID
    var wtEl = document.getElementById('pad-edit-widget-type');
    var provInput = document.getElementById('pad-edit-list-provider-id');
    var provId = provInput ? provInput.value.trim() : '';
    if (!wtEl || wtEl.value !== 'list' || !provId) {
        restorePrev();
        return;
    }
    var title = provId.charAt(0).toUpperCase() + provId.slice(1);
    var opt = document.createElement('option');
    opt.value = '[list:' + provId + '.selected]';
    opt.textContent = 'Selected ' + title + ' Item';
    opt.setAttribute('data-synthetic', '1');
    // Insert after "(none)" option
    if (sel.options.length > 1) {
        sel.insertBefore(opt, sel.options[1]);
    } else {
        sel.appendChild(opt);
    }
    // Re-apply pending value if it matches the newly injected synthetic option
    if (sel.hasAttribute('data-pending-value') &&
        sel.getAttribute('data-pending-value') === opt.value) {
        sel.value = opt.value;
        sel.removeAttribute('data-pending-value');
    } else {
        // Restore the previous selection (may be the just-injected synthetic option)
        restorePrev();
    }
}

// Refresh synthetic options in all tap and long-press action screen dropdowns.
// Called when widget type changes to/from "list" or when provider ID changes.
function listRefreshSyntheticOptions() {
    for (var i = 0; i < (typeof MAX_ACTIONS !== 'undefined' ? MAX_ACTIONS : 3); i++) {
        listInjectSyntheticScreenOption('pad-edit-action-' + i);
        listInjectSyntheticScreenOption('pad-edit-lp-action-' + i);
    }
}

// Populate the screen target dropdown(s) for one or more action editor prefixes.
// screens: array of { id, name } from deviceInfoCache.available_screens
// prefixes: array of prefix strings
function actionEditorPopulateScreens(prefixes, screens) {
    if (!screens) return;
    prefixes.forEach(function(prefix) {
        var sel = document.getElementById(prefix + '-target');
        if (!sel) return;
        var selected = sel.getAttribute('data-pending-value') || sel.value;
        while (sel.options.length > 1) sel.remove(1);
        screens.forEach(function(s) {
            var opt = document.createElement('option');
            opt.value = s.id;
            opt.textContent = s.name;
            sel.appendChild(opt);
        });
        listInjectSyntheticScreenOption(prefix);
        sel.value = selected;
        if (sel.value === selected) sel.removeAttribute('data-pending-value');
        else if (selected) sel.setAttribute('data-pending-value', selected);
    });
}

_actionEditorExtensions.push({
    type: 'screen',
    groups: function(prefix, opts) {
        var h = '';
        // Screen target
        h += '<div id="' + prefix + '-screen-group" style="display:none;">';
        h += '<div class="form-group">';
        h += '<label class="form-label" for="' + prefix + '-target">Target Screen</label>';
        h += '<select class="form-select form-select-sm" id="' + prefix + '-target"><option value="">(none)</option></select>';
        h += '</div></div>';
        return h;
    },
    typeChanged: function(prefix, type) {
        var screenGrp = document.getElementById(prefix + '-screen-group');
        if (screenGrp) screenGrp.style.display = (type === 'screen') ? '' : 'none';
        if (type === 'screen') {
            listInjectSyntheticScreenOption(prefix);
            var tgt = document.getElementById(prefix + '-target');
            if (tgt && tgt.hasAttribute('data-pending-value')) {
                var pv = tgt.getAttribute('data-pending-value');
                tgt.value = pv;
                if (tgt.value === pv) tgt.removeAttribute('data-pending-value');
            }
        }
    },
    load: function(prefix, action) {
        var el;
        el = document.getElementById(prefix + '-target');
        if (el) {
            el.removeAttribute('data-pending-value');
            el.value = action.target || '';
            // If the option doesn't exist (e.g., synthetic [list:…] not yet injected),
            // defer the value until the option is added.
            if (action.target && el.value !== action.target) {
                el.setAttribute('data-pending-value', action.target);
                el.value = '';
            }
        }
    },
    build: function(prefix, type) {
        if (type !== 'screen') return null;
        var act = {};
        var t = document.getElementById(prefix + '-target');
        if (t) act.target = t.value;
        return act;
    },
    wireFragment: function(prefixes, info) {
        actionEditorPopulateScreens(prefixes, info && info.available_screens);
    }
});
