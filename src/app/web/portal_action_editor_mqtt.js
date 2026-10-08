
_actionEditorExtensions.push({
    type: 'mqtt',
    bindingSuffixes: ["-topic","-payload"],
    groups: function(prefix, opts) {
        var h = '';
        // MQTT
        h += '<div id="' + prefix + '-mqtt-group" style="display:none;">';
        h += '<div class="form-group">';
        h += '<label class="form-label" for="' + prefix + '-topic">MQTT Topic <span class="fx-hint" onclick="showBindingHelp()">fx</span></label>';
        h += '<input type="text" class="form-control form-control-sm" id="' + prefix + '-topic" maxlength="127" placeholder="e.g. home/light/toggle">';
        h += '</div>';
        h += '<div class="form-group">';
        h += '<label class="form-label" for="' + prefix + '-payload">MQTT Payload <span class="fx-hint" onclick="showBindingHelp()">fx</span></label>';
        h += '<input type="text" class="form-control form-control-sm" id="' + prefix + '-payload" maxlength="127" placeholder="e.g. ON or [health:cpu]">';
        h += '</div></div>';
        return h;
    },
    typeChanged: function(prefix, type) {
        var group = document.getElementById(prefix + '-mqtt-group');
        if (group) group.style.display = type === 'mqtt' ? '' : 'none';
    },
    load: function(prefix, action) {
        var el;
        el = document.getElementById(prefix + '-topic');
        if (el) el.value = action.topic || '';
        el = document.getElementById(prefix + '-payload');
        if (el) el.value = action.payload || '';
    },
    build: function(prefix, type) {
        if (type !== 'mqtt') return null;
        var act = {};
        var topic = document.getElementById(prefix + '-topic');
        var payload = document.getElementById(prefix + '-payload');
        if (topic) act.topic = (topic.value || '').trim();
        if (payload) act.payload = (payload.value || '').trim();
        return act;
    }
});
