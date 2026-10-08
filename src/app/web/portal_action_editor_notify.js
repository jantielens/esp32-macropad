
_actionEditorExtensions.push({
    type: 'notify',
    bindingSuffixes: ["-notify-text","-notify-duration"],
    colorSuffixes: ["-notify-text-color-wrap","-notify-bg-color-wrap","-notify-border-color-wrap"],
    groups: function(prefix, opts) {
        var h = '';
        // Notify
        h += '<div id="' + prefix + '-notify-group" style="display:none;">';
        h += '<div class="form-group">';
        h += '<label class="form-label" for="' + prefix + '-notify-text">Message <span class="fx-hint" onclick="showBindingHelp()">fx</span></label>';
        h += '<input type="text" class="form-control form-control-sm" id="' + prefix + '-notify-text" maxlength="127" placeholder="e.g. Brightness is at 100%">';
        h += '<small>Supports binding templates. Empty = dismiss current notification.</small>';
        h += '</div>';
        h += '<div class="form-group">';
        h += '<label class="form-label" for="' + prefix + '-notify-duration">Duration (ms) <span class="fx-hint" onclick="showBindingHelp()">fx</span></label>';
        h += '<input type="text" class="form-control form-control-sm" id="' + prefix + '-notify-duration" value="3000" placeholder="3000">';
        h += '<small>0 = persistent (tap to dismiss). Supports bindings.</small>';
        h += '</div>';
        h += '<div class="grid-2col">';
        h += '<div class="form-group">';
        h += '<label style="font-size:13px; font-weight:600; margin-bottom:2px; display:block;">Text Color <span class="fx-hint" onclick="showBindingHelp()">fx</span></label>';
        h += '<div class="bindable-color" id="' + prefix + '-notify-text-color-wrap"><div class="bc-swatch"></div>';
        h += '<input type="text" id="' + prefix + '-notify-text-color" class="bc-input" maxlength="191" spellcheck="false" placeholder="#ffffff or [binding]"></div>';
        h += '</div>';
        h += '<div class="form-group">';
        h += '<label style="font-size:13px; font-weight:600; margin-bottom:2px; display:block;">Background Color <span class="fx-hint" onclick="showBindingHelp()">fx</span></label>';
        h += '<div class="bindable-color" id="' + prefix + '-notify-bg-color-wrap"><div class="bc-swatch"></div>';
        h += '<input type="text" id="' + prefix + '-notify-bg-color" class="bc-input" maxlength="191" spellcheck="false" placeholder="#333333 or [binding]"></div>';
        h += '</div>';
        h += '</div>';
        h += '<div class="grid-2col">';
        h += '<div class="form-group">';
        h += '<label style="font-size:13px; font-weight:600; margin-bottom:2px; display:block;">Border Color <span class="fx-hint" onclick="showBindingHelp()">fx</span></label>';
        h += '<div class="bindable-color" id="' + prefix + '-notify-border-color-wrap"><div class="bc-swatch"></div>';
        h += '<input type="text" id="' + prefix + '-notify-border-color" class="bc-input" maxlength="191" spellcheck="false" placeholder="Empty = no border"></div>';
        h += '</div>';
        h += '<div class="form-group">';
        h += '<label class="form-label" for="' + prefix + '-notify-opacity">Opacity (%)</label>';
        h += '<input type="number" class="form-control form-control-sm" id="' + prefix + '-notify-opacity" min="0" max="100" placeholder="85">';
        h += '</div>';
        h += '</div>';
        h += '<div class="grid-2col">';
        h += '<div class="form-group">';
        h += '<label class="form-label" for="' + prefix + '-notify-font-size">Font Size</label>';
        h += '<input type="number" class="form-control form-control-sm" id="' + prefix + '-notify-font-size" min="0" max="48" placeholder="0 = auto">';
        h += '</div>';
        h += '<div class="form-group">';
        h += '<label class="form-label" for="' + prefix + '-notify-location">Location</label>';
        h += '<select class="form-select form-select-sm" id="' + prefix + '-notify-location">';
        h += '<option value="bottom" selected>Bottom</option>';
        h += '<option value="center">Center</option>';
        h += '<option value="top">Top</option>';
        h += '</select>';
        h += '</div>';
        h += '</div>';
        h += '</div>';
        return h;
    },
    typeChanged: function(prefix, type) {
        var group = document.getElementById(prefix + '-notify-group');
        if (group) group.style.display = type === 'notify' ? '' : 'none';
    },
    load: function(prefix, action) {
        var el;
        // Notify fields
        el = document.getElementById(prefix + '-notify-text');
        if (el) { el.value = action.notify_text || ''; }
        el = document.getElementById(prefix + '-notify-duration');
        if (el) { el.value = action.notify_duration_ms || '3000'; }
        padSetBindableColor(prefix + '-notify-text-color', action.notify_text_color || '', '#ffffff');
        padSetBindableColor(prefix + '-notify-bg-color', action.notify_bg_color || '', '#333333');
        padSetBindableColor(prefix + '-notify-border-color', action.notify_border_color || '', '');
        el = document.getElementById(prefix + '-notify-opacity');
        if (el) el.value = (action.notify_opacity > 0) ? action.notify_opacity : '';
        el = document.getElementById(prefix + '-notify-font-size');
        if (el) el.value = (action.notify_font_size > 0) ? action.notify_font_size : '';
        el = document.getElementById(prefix + '-notify-location');
        if (el) el.value = action.notify_location || 'bottom';
    },
    build: function(prefix, type) {
        if (type !== 'notify') return null;
        var act = {};
        var nt = document.getElementById(prefix + '-notify-text');
        if (nt) act.notify_text = (nt.value || '').trim();
        var nd = document.getElementById(prefix + '-notify-duration');
        if (nd && nd.value !== '') act.notify_duration_ms = (nd.value || '').trim();
        var ntc = padGetBindableColor(prefix + '-notify-text-color');
        if (ntc) act.notify_text_color = ntc;
        var nbc = padGetBindableColor(prefix + '-notify-bg-color');
        if (nbc) act.notify_bg_color = nbc;
        var nbrc = padGetBindableColor(prefix + '-notify-border-color');
        if (nbrc) act.notify_border_color = nbrc;
        var nop = document.getElementById(prefix + '-notify-opacity');
        if (nop && nop.value !== '') act.notify_opacity = parseInt(nop.value, 10);
        var nfs = document.getElementById(prefix + '-notify-font-size');
        if (nfs && nfs.value !== '') act.notify_font_size = parseInt(nfs.value, 10);
        var nloc = document.getElementById(prefix + '-notify-location');
        if (nloc) act.notify_location = nloc.value;
        return act;
    }
});
