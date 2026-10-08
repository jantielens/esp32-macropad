function actionEditorSoundAlertChanged(prefix) {
    var kind = document.getElementById(prefix + '-sound-alert-kind');
    var tone = document.getElementById(prefix + '-sound-alert-tone-group');
    var mp3 = document.getElementById(prefix + '-sound-alert-mp3-group');
    var isMp3 = kind && kind.value === 'mp3';
    if (tone) tone.style.display = isMp3 || (kind && kind.value === 'stop') ? 'none' : '';
    if (mp3) mp3.style.display = isMp3 ? '' : 'none';
}

function actionEditorPopulateSounds(prefixes, sounds) {
    if (!sounds) return;
    prefixes.forEach(function(prefix) {
        var selects = [document.getElementById(prefix + '-sound-alert-file')];
        actionEditorCatalog().forEach(function(entry) {
            actionEditorGenericFields(entry.type).forEach(function(field) {
                if (field.options_source === 'sounds') selects.push(document.getElementById(prefix + '-generic-' + entry.type + '-' + field.name));
            });
        });
        selects.forEach(function(sel) {
            if (!sel) return;
            var selected = sel.value;
            while (sel.options.length > 1) sel.remove(1);
            var names = sounds.slice();
            if (selected && names.indexOf(selected) < 0) names.push(selected);
            names.forEach(function(name) {
                var opt = document.createElement('option');
                opt.value = name;
                opt.textContent = name;
                sel.appendChild(opt);
            });
            sel.value = selected;
        });
    });
}

_actionEditorExtensions.push({
    type: 'sound_alert',
    bindingSuffixes: ["-sound-alert-pattern"],
    groups: function(prefix, opts) {
        var h = '';
        // Sound Alert
        h += '<div id="' + prefix + '-sound-alert-group" style="display:none;">';
        h += '<div class="form-group"><label class="form-label" for="' + prefix + '-sound-alert-kind">Kind</label>';
        h += '<select class="form-select form-select-sm" id="' + prefix + '-sound-alert-kind" onchange="actionEditorSoundAlertChanged(\'' + prefix + '\')">';
        h += '<option value="tone">Tone Alert</option><option value="mp3">MP3 Alert</option><option value="stop">Stop audio</option></select></div>';
        h += '<div id="' + prefix + '-sound-alert-tone-group">';
        h += '<div class="form-group"><label class="form-label" for="' + prefix + '-sound-alert-pattern">Tone pattern <span class="fx-hint" onclick="showBindingHelp()">fx</span></label>';
        h += '<input type="text" class="form-control form-control-sm" id="' + prefix + '-sound-alert-pattern" maxlength="127" placeholder="e.g. 1000:200 100 1000:200"></div></div>';
        h += '<div id="' + prefix + '-sound-alert-mp3-group" style="display:none;">';
        h += '<div class="form-group">';
        h += '<label class="form-label" for="' + prefix + '-sound-alert-file">MP3 file</label>';
        h += '<select class="form-select form-select-sm" id="' + prefix + '-sound-alert-file"><option value="">(none)</option></select>';
        h += '</div>';
        h += '<div class="form-group">';
        h += '<label class="form-label" for="' + prefix + '-sound-alert-volume">Volume override (%)</label>';
        h += '<input type="number" class="form-control form-control-sm" id="' + prefix + '-sound-alert-volume" min="0" max="100" placeholder="(use device volume)">';
        h += '<small>Empty or 0 = use device volume from Home &rarr; Audio; 1-100 overrides it.</small>';
        h += '</div></div></div>';
        return h;
    },
    typeChanged: function(prefix, type) {
        var group = document.getElementById(prefix + '-sound-alert-group');
        if (group) group.style.display = type === 'sound_alert' ? '' : 'none';
        if (type === 'sound_alert') actionEditorSoundAlertChanged(prefix);
    },
    load: function(prefix, action) {
        var el;
        el = document.getElementById(prefix + '-sound-alert-kind');
        if (el) el.value = action.sound_alert_kind || 'tone';
        el = document.getElementById(prefix + '-sound-alert-pattern');
        if (el) el.value = action.sound_alert_pattern || '';
        el = document.getElementById(prefix + '-sound-alert-file');
        if (el) {
            if (action.sound_alert_file && !Array.from(el.options).some(function(option) { return option.value === action.sound_alert_file; })) {
                var soundOption = document.createElement('option');
                soundOption.value = action.sound_alert_file;
                soundOption.textContent = action.sound_alert_file;
                el.appendChild(soundOption);
            }
            el.value = action.sound_alert_file || '';
            if (el.selectedIndex < 0) el.value = '';
        }
        el = document.getElementById(prefix + '-sound-alert-volume');
        if (el) el.value = (action.sound_alert_volume > 0) ? action.sound_alert_volume : '';

    },
    build: function(prefix, type) {
        if (type !== 'sound_alert') return null;
        var act = {};
        var kind = document.getElementById(prefix + '-sound-alert-kind');
        act.sound_alert_kind = kind ? kind.value : 'tone';
        var volume = document.getElementById(prefix + '-sound-alert-volume');
        if (volume && volume.value !== '') act.sound_alert_volume = parseInt(volume.value, 10);
        if (act.sound_alert_kind === 'tone' || act.sound_alert_kind === 'tone_loop') {
            var pattern = document.getElementById(prefix + '-sound-alert-pattern');
            if (pattern) act.sound_alert_pattern = (pattern.value || '').trim();
        } else if (act.sound_alert_kind === 'mp3') {
            var file = document.getElementById(prefix + '-sound-alert-file');
            if (file) act.sound_alert_file = file.value || '';
        }
        return act;
    },
    wireFragment: function(prefixes, info) {
        if (!info || info.has_sound_player !== true) return;
        return fetch('/api/sounds/list').then(function(response) { return response.ok ? response.json() : []; })
            .then(function(sounds) { actionEditorPopulateSounds(prefixes, sounds); })
            .catch(function() {});
    }
});
