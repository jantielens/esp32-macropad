const assert = require('node:assert/strict');
const {execFileSync} = require('node:child_process');
const path = require('node:path');
const os = require('node:os');
const vm = require('node:vm');
const zlib = require('node:zlib');

const root = path.resolve(__dirname, '..');
const fs = require('node:fs');
const nativeDirectory = fs.mkdtempSync(path.join(os.tmpdir(), 'dialog-gates-'));
try {
    for (const buttons of [0, 1]) {
        for (const module of [
            'device_dialog.cpp', 'dialog_appearance.cpp', 'actions/notify_action.cpp', 'actions/confirm_action.cpp',
            'pad_binding.cpp', 'pad_resolve_request.cpp', 'data_stream.cpp', 'web_portal_pad.cpp',
            'widgets/bar_chart_widget.cpp', 'widgets/gauge_widget.cpp', 'widgets/sparkline_widget.cpp'
        ]) {
            const object = path.join(nativeDirectory, 'module.o');
            execFileSync('c++', ['-std=c++17', '-c', 'src/app/' + module, '-o', object,
                '-DBOARD_CONFIG_H', '-DHAS_DISPLAY=0', '-DHAS_BUTTON=' + buttons,
                '-Itests', '-Isrc/app'], {cwd: root, stdio: 'pipe'});
            assert.equal(execFileSync('nm', ['--defined-only', object], {encoding: 'utf8'}).trim(), '',
                module + ' must emit no code or data without a display (HAS_BUTTON=' + buttons + ')');
        }
    }
} finally {
    fs.rmSync(nativeDirectory, {recursive: true, force: true});
}
const headlessMqttObject = path.join(os.tmpdir(), 'mqtt-inspection-headless-' + process.pid + '.o');
try {
    execFileSync('c++', ['-std=c++17', '-c', 'src/app/mqtt_inspection.cpp', '-o', headlessMqttObject,
        '-DBOARD_CONFIG_H', '-DHAS_DISPLAY=0', '-DHAS_MQTT=1',
        '-Itests', '-Isrc/app'], {cwd: root, stdio: 'pipe'});
    assert.equal(execFileSync('nm', ['--defined-only', headlessMqttObject], {encoding: 'utf8'}).trim(), '',
        'MQTT inspection must emit no code or data without a display');
} finally {
    fs.rmSync(headlessMqttObject, {force: true});
}
const widgets = JSON.parse(fs.readFileSync(path.join(root, 'src/app/widgets/manifest.json'), 'utf8'));
const expanded = execFileSync('python3', ['tools/widget_manifest.py', '--bundle', 'src/app/web/portal.js.bundle'],
    {cwd: root, encoding: 'utf8'});
const modules = expanded.split('\n').filter(line => /^portal_widget_/.test(line) && line !== 'portal_widget_editor.js');
assert.equal(modules.length, new Set(modules).size, 'Shared widget modules must execute once');
assert.equal(modules.length, new Set(widgets.filter(widget => widget.portal).map(widget => widget.portal.js)).size);
execFileSync('bash', ['tools/minify-web-assets.sh', 'esp32-macropad', 'ESP32 Macropad'],
    {cwd: root, stdio: 'pipe'});

for (const [board, target, psram, integrationMode = 'default', epaperProfile = false] of [
    ['esp32c3-withsensors', 'ESP32C3', false],
    ['esp32-p4-lcd4b', 'ESP32P4', true],
    ['esp32-p4-lcd4b', 'ESP32P4', false],
    ['esp32-p4-lcd4b', 'ESP32P4', false, 'builtin-only'],
    ['esp32-p4-lcd4b', 'ESP32P4', true, 'ha-only'],
    ['reterminal-e1003-frame', 'ESP32S3', true, 'default', true]
]) {
    const source = execFileSync('c++', ['-E', '-P', '-x', 'c++', 'src/app/web_assets.h',
        '-Itests', '-Isrc/boards/' + board, '-DBOARD_HAS_OVERRIDE', '-DCONFIG_IDF_TARGET_' + target,
        ...(psram ? ['-DBOARD_HAS_PSRAM'] : []),
        ...(integrationMode === 'builtin-only' ? ['-DHAS_MQTT=0', '-DHAS_HA_LIVE=0'] : []),
        ...(integrationMode === 'ha-only' ? ['-DHAS_MQTT=0', '-DHAS_HA_LIVE=1'] : [])],
        {cwd: root, encoding: 'utf8', maxBuffer: 8 * 1024 * 1024, stdio: ['ignore', 'pipe', 'pipe']});
    const assets = new Map();
    const pattern = /const uint8_t (\w+_gz)\[\] PROGMEM = \{([\s\S]*?)\};/g;
    for (const match of source.matchAll(pattern)) {
        assert(!assets.has(match[1]), 'Duplicate selected asset: ' + match[1]);
        const bytes = Buffer.from([...match[2].matchAll(/0x([0-9a-f]{2})/g)].map(value => parseInt(value[1], 16)));
        assets.set(match[1], zlib.gunzipSync(bytes).toString());
    }
    assert(assets.has('portal_js_gz'));
    if (!epaperProfile) {
        assert(!assets.has('epaper_refresh_fragment_html_gz'));
        assert(!assets.has('portal_epaper_refresh_js_gz'));
    }
    for (const [name, text] of assets) {
        if (!epaperProfile) {
            assert(!text.includes('epaper_bw_conversion'), 'E-paper conversion leaked into ' + board + '/' + name);
            assert(!text.includes('epaper_bw_threshold'), 'E-paper threshold leaked into ' + board + '/' + name);
        }
        if (name.endsWith('_js_gz')) new vm.Script(text, {filename: board + '/' + name});
    }
    const js = assets.get('portal_js_gz');
    assert(js.includes('function uploadFirmware('), 'HTML upload callback was removed');
    assert(js.includes('function saveFragmentConfig('), 'Fragment save callback was removed');
    const display = board === 'esp32-p4-lcd4b';
    const ha = display && psram && integrationMode !== 'builtin-only';
    const mqtt = display && !['builtin-only', 'ha-only'].includes(integrationMode);
    const shell = assets.get('shell_html_gz');
    assert.equal(shell.includes('id="binding-help-overlay"'), display, 'Binding dialog HTML must follow HAS_DISPLAY');
    assert.equal(js.includes('function haBindingBuilderHTML('), ha, 'HA builder JS must follow HAS_HA_LIVE');
    assert.equal(js.includes('const haButtonBuilderProvider='), ha, 'HA button source provider must follow HAS_HA_LIVE');
    assert.equal(js.includes('function padButtonBuilderOpen('), display, 'Shared button builder must follow HAS_DISPLAY');
    assert.equal(js.includes('const padButtonBuilders='), display, 'Shared button templates must follow HAS_DISPLAY');
    assert.equal(js.includes('function bindingBuilderToken('), display, 'Shared builder helpers must follow HAS_DISPLAY');
    assert.equal(js.includes('function bindingBuilderHTML('), display);
    assert.equal(js.includes('function bindingBuilderFormatControlsHTML('), display);
    assert.equal(js.includes('const catalogBindingProvider='), display,
        'Catalog binding provider must follow HAS_DISPLAY');
    assert.equal(js.includes('const mqttBindingProvider='), mqtt,
        'MQTT binding provider must require HAS_MQTT and HAS_DISPLAY');
    assert.equal(js.includes('catalogButtonBuilderProvider='), display,
        'Catalog button scenarios must be available without integrations');
    assert.equal(js.includes('mqttButtonBuilderProvider='), mqtt,
        'MQTT button scenarios must follow the binding-provider gates');
    if (display) {
        const shared = js.indexOf('const BUTTON_BUILDER_PROVIDERS=');
        const templates = js.indexOf('const padButtonBuilders=');
        const dialog = js.indexOf('function padButtonBuilderOpen(');
        assert(shared >= 0 && shared < templates && templates < dialog,
            'Shared provider registry and scenarios must precede the button dialog');
        for (const [provider, enabled] of [
            ['catalogButtonBuilderProvider=', true],
            ['mqttButtonBuilderProvider=', mqtt],
            ['const haButtonBuilderProvider=', ha]
        ]) {
            if (enabled) assert(shared < js.indexOf(provider) && js.indexOf(provider) < templates,
                provider + ' must register after the shared registry and before scenarios');
        }
    }
    assert.equal(js.includes('Preview on device'), display, 'Shared preview controls must follow HAS_DISPLAY');
    assert.equal(js.includes("Build a Home Assistant binding"), ha, 'HA shortcut registration must follow HAS_HA_LIVE');
    assert.equal(assets.has('dialog_appearance_fragment_html_gz'), display);
    assert.equal(js.includes('init_dialog_appearance_fragment'), display);
    const css = assets.get('portal_all_css_gz');
    assert(css, 'Main stylesheet must be selected');
    assert.equal(css.includes('.binding-docs-modal'), display, 'Binding reference CSS must follow HAS_DISPLAY');
    assert.equal(css.includes('.binding-builder-shortcut'), display, 'Binding shortcut CSS must follow HAS_DISPLAY');
    assert.equal(css.includes('.binding-builder-step'), display, 'Shared builder CSS must follow HAS_DISPLAY');
    assert.equal(css.includes('.pad-button-builder-entry'), display, 'Button builder CSS must follow HAS_DISPLAY');
    assert.equal(css.includes('.dialog-appearance-layout'), display);
    assert.equal(assets.has('fonts_fragment_html_gz'), display && psram);
    assert.equal(assets.has('portal_fonts_js_gz'), display && psram);
    if (display && psram) {
        const fonts = assets.get('fonts_fragment_html_gz');
        const idPattern = fonts.match(/pattern="([^"]+)"/)[1];
        const validId = new RegExp('^(?:' + idPattern + ')$', 'v');
        assert(validId.test('my-font_1'));
        assert(!validId.test('Bad.ttf'));
        assert(!validId.test('../font'));
        assert(fonts.includes('portal-icon-fallback'));
        assert(assets.get('portal_fonts_js_gz').includes('portal-icon-fallback'));
        assert(!fonts.includes('data-icon='));
    }
    assert.equal(js.includes('function showBindingHelp('), display);
    assert.equal(js.includes('function bindingValidateHomeConfig('), display);
    assert.equal(js.includes('init_extensions_fragment'), display);
    if (!epaperProfile) {
        for (const stem of ['mcp', 'recipes', 'recipe_catalog', 'extensions']) {
            assert.equal(assets.has(stem + '_fragment_html_gz'), display, stem);
        }
    }
    if (!display && !epaperProfile) {
        for (const stem of ['music', 'camera_motion', 'mqtt_triggers', 'hw_buttons', 'feedback', 'storage']) {
            assert(!assets.has(stem + '_fragment_html_gz'), 'Headless-unused asset: ' + stem);
        }
    }
    console.log('PASS: selected, valid minified assets for ' + board +
        ' (PSRAM=' + psram + ', integrations=' + integrationMode + ')');
}