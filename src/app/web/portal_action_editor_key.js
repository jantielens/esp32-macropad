
_actionEditorExtensions.push({
    type: 'key',
    bindingSuffixes: ["-sequence"],
    groups: function(prefix, opts) {
        var h = '';
        if (opts.showBleHint) h += '<small id="' + prefix + '-ble-hint" style="display:none;">Keyboard unavailable on this board.</small>';
        // Key sequence
        h += '<div id="' + prefix + '-key-group" style="display:none;">';
        h += '<div class="form-group">';
        h += '<label class="form-label" for="' + prefix + '-sequence">Keys to Send <span class="fx-hint" onclick="showBindingHelp()">fx</span></label>';
        h += '<input type="text" class="form-control form-control-sm" id="' + prefix + '-sequence" maxlength="255" placeholder=\'e.g. ctrl+c, "hello", 200ms\'>';
        if (opts.showKeyHelp) {
            h += '<small>Space-separated steps. <b>Modifiers:</b> ctrl, shift, alt, gui &mdash; <b>Keys:</b> a&ndash;z, 0&ndash;9, enter, tab, esc, space, backspace, delete, up/down/left/right, f1&ndash;f12, home, end, pageup, pagedown, insert, printscreen, capslock &mdash; <b>Media:</b> vol_up, vol_down, mute, play_pause, next_track, prev_track &mdash; <b>Combos:</b> ctrl+c, ctrl+shift+t, gui+l &mdash; <b>Text:</b> &quot;hello&quot; &mdash; <b>Delay:</b> 200ms. Supports bindings.</small>';
        }
        h += '</div></div>';
        return h;
    },
    typeChanged: function(prefix, type) {
        var group = document.getElementById(prefix + '-key-group');
        if (group) group.style.display = type === 'key' ? '' : 'none';
        var hint = document.getElementById(prefix + '-ble-hint');
        if (hint) hint.style.display = type === 'key' || type === 'ble_pair' ? '' : 'none';
    },
    load: function(prefix, action) {
        var el;
        el = document.getElementById(prefix + '-sequence');
        if (el) el.value = action.sequence || '';
    },
    build: function(prefix, type) {
        if (type !== 'key') return null;
        var act = {};
        var seq = document.getElementById(prefix + '-sequence');
        if (seq) act.sequence = (seq.value || '').trim();
        return act;
    }
});
