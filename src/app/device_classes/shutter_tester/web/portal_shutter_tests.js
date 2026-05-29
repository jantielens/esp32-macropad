// portal_shutter_tests.js — Guided test definition editor: load, save, validate.
// Integrated via portal_fragment_init.js init_shutter_tests_fragment().

// Valid standard speeds (must match STANDARD_SPEEDS in shutter_measure.cpp)
var _VALID_SPEEDS = [
    '4s','2s','1s','1/2s','1/4s','1/5s','1/8s','1/10s','1/15s','1/25s',
    '1/30s','1/50s','1/60s','1/100s','1/125s','1/200s','1/250s','1/500s',
    '1/1000s','1/2000s'
];

function shutterTestsReload() {
    var loading = document.getElementById('shutter-tests-loading');
    var editor  = document.getElementById('shutter-tests-editor');
    var status  = document.getElementById('shutter-tests-status');
    if (loading) loading.style.display = '';
    if (editor)  editor.style.display  = 'none';
    if (status)  status.style.display  = 'none';

    fetch('/api/shutter/tests')
        .then(function(r) { return r.text(); })
        .then(function(text) {
            var ta = document.getElementById('shutter-tests-textarea');
            if (ta) ta.value = text;
            if (loading) loading.style.display = 'none';
            if (editor)  editor.style.display  = '';
            shutterTestsValidate();
            shutterTestsRefreshList();
        })
        .catch(function() {
            if (loading) loading.textContent = 'Failed to load guided test definitions.';
        });
}

function shutterTestsSave() {
    var ta = document.getElementById('shutter-tests-textarea');
    if (!ta) return;

    var status = document.getElementById('shutter-tests-status');

    fetch('/api/shutter/tests', {
        method: 'PUT',
        headers: { 'Content-Type': 'text/plain' },
        body: ta.value
    })
    .then(function(r) { return r.json(); })
    .then(function(j) {
        if (status) {
            status.style.display = '';
            status.className = 'small mt-2 text-' + (j.success ? 'success' : 'danger');
            status.textContent = j.success ? '✓ Saved' : '✗ Save failed';
            setTimeout(function() { status.style.display = 'none'; }, 3000);
        }
        if (j.success) shutterTestsRefreshList();
    })
    .catch(function() {
        if (status) {
            status.style.display = '';
            status.className = 'small mt-2 text-danger';
            status.textContent = '✗ Network error';
        }
    });
}

function shutterTestsValidate() {
    var ta = document.getElementById('shutter-tests-textarea');
    var vd = document.getElementById('shutter-tests-validation');
    if (!ta || !vd) return;

    var lines = ta.value.split('\n');
    var warnings = [];

    for (var i = 0; i < lines.length; i++) {
        var line = lines[i].trim();
        if (!line || line.charAt(0) === '#') continue;
        if (line.indexOf(':') >= 0) continue;  // key: value line

        // Speed line — validate
        var speed = line;
        // Normalize: add trailing 's' if missing
        if (speed.charAt(speed.length - 1) !== 's') speed = speed + 's';
        if (_VALID_SPEEDS.indexOf(speed) < 0) {
            warnings.push('Line ' + (i + 1) + ': unknown speed "' + lines[i].trim() + '"');
        }
    }

    if (warnings.length > 0) {
        vd.style.display = '';
        vd.className = 'small mb-2 text-warning';
        vd.innerHTML = '⚠ ' + warnings.join('<br>⚠ ');
    } else {
        vd.style.display = 'none';
    }
}

function shutterTestsRefreshList() {
    fetch('/api/shutter/tests/list')
        .then(function(r) { return r.json(); })
        .then(function(tests) {
            var card = document.getElementById('shutter-tests-list-card');
            var list = document.getElementById('shutter-tests-parsed-list');
            if (!list || !card) return;

            if (!tests || tests.length === 0) {
                card.style.display = 'none';
                return;
            }

            card.style.display = '';
            var html = '<table class="table table-sm table-striped mb-0">';
            html += '<thead><tr><th>ID</th><th>Name</th><th>Speeds</th><th>Shots/Speed</th></tr></thead><tbody>';
            for (var i = 0; i < tests.length; i++) {
                var t = tests[i];
                html += '<tr><td><code>' + _escHtml(t.id) + '</code></td>'
                      + '<td>' + _escHtml(t.name) + '</td>'
                      + '<td>' + t.speed_count + '</td>'
                      + '<td>' + t.shots_per_speed + '</td></tr>';
            }
            html += '</tbody></table>';
            list.innerHTML = html;
        })
        .catch(function() {});
}

function _escHtml(s) {
    var d = document.createElement('div');
    d.appendChild(document.createTextNode(s));
    return d.innerHTML;
}

// ============================================================================
// Fragment init — called by portal_nav.js when the shutter-tests fragment loads
// ============================================================================
window.init_shutter_tests_fragment = function () {
    shutterTestsReload();
};
