const assert = require('node:assert/strict');
const {execFileSync} = require('node:child_process');
const path = require('node:path');
const os = require('node:os');
const vm = require('node:vm');
const zlib = require('node:zlib');
const fs = require('node:fs');

const root = path.resolve(__dirname, '..');
const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'portal-gates-'));
try {
    for (const buttons of [0, 1]) {
        for (const module of [
            'device_dialog.cpp', 'dialog_appearance.cpp', 'actions/notify_action.cpp', 'actions/confirm_action.cpp',
            'pad_binding.cpp', 'pad_resolve_request.cpp', 'data_stream.cpp', 'web_portal_pad.cpp',
            'widgets/bar_chart_widget.cpp', 'widgets/gauge_widget.cpp', 'widgets/sparkline_widget.cpp',
            'widgets/face_widget.cpp'
        ]) {
            const object = path.join(directory, 'module.o');
            execFileSync('c++', ['-std=c++17', '-c', 'src/app/' + module, '-o', object,
                '-DBOARD_CONFIG_H', '-DHAS_DISPLAY=0', '-DHAS_BUTTON=' + buttons,
                '-Itests', '-Isrc/app'], {cwd: root, stdio: 'pipe'});
            assert.equal(execFileSync('nm', ['--defined-only', object], {encoding: 'utf8'}).trim(), '',
                module + ' must emit no code or data without a display (HAS_BUTTON=' + buttons + ')');
        }
    }
    for (const [flag, modules] of [
        ['HAS_EXTERNAL_DATA', ['external_data.cpp', 'external_data_config.cpp', 'ext_binding.cpp']],
        ['HAS_VOICE_NOTES', ['voice_notes.cpp', 'voice_notes_binding.cpp', 'voice_note_range.cpp', 'voice_notes_metadata.cpp',
            'voice_notes_processing.cpp', 'voice_notes_transcription_request.cpp',
            'voice_notes_enrichment.cpp', 'voice_notes_enrichment_request.cpp']],
        ['HAS_AUTO_SHUTDOWN', ['auto_shutdown.cpp']],
        ['HAS_AUDIO_INPUT', ['audio_input.cpp', 'web_portal_live_microphone.cpp']]
    ]) {
        for (const module of modules) {
            const object = path.join(directory, 'disabled.o');
            execFileSync('c++', ['-std=c++17', '-c', 'src/app/' + module, '-o', object,
                '-DBOARD_CONFIG_H', '-D' + flag + '=0', '-DHAS_MICROPHONE_STREAMING=0', '-Itests', '-Isrc/app'],
                {cwd: root, stdio: 'pipe'});
            assert.equal(execFileSync('nm', ['--defined-only', object], {encoding: 'utf8'}).trim(), '', module);
        }
    }
    for (const streaming of [0, 1]) {
        const object = path.join(directory, 'microphone-component.o');
        execFileSync('c++', ['-std=c++17', '-c', 'src/app/components/microphone_component.cpp',
            '-o', object, '-DHAS_AUDIO_INPUT=1', '-DHAS_MICROPHONE_STREAMING=' + streaming,
            '-Itests/live_microphone_overrides', '-Itests/audio_input_overrides', '-Itests', '-Isrc/app',
            '-I' + path.join(os.homedir(), 'Arduino/libraries/ArduinoJson/src'),
            '-include', 'tests/log_manager.h', '-include', 'tests/board_config.h'],
            {cwd: root, stdio: 'pipe'});
        const symbols = execFileSync('nm', ['-C', object], {encoding: 'utf8'});
        assert.equal(symbols.includes('handleLiveMicrophoneStream'), !!streaming);
        assert(symbols.includes('microphone_levels'));
        assert(symbols.includes('microphone_settings_body'));
    }
    const mqttObject = path.join(directory, 'mqtt.o');
    execFileSync('c++', ['-std=c++17', '-c', 'src/app/mqtt_inspection.cpp', '-o', mqttObject,
        '-DBOARD_CONFIG_H', '-DHAS_DISPLAY=0', '-DHAS_MQTT=1', '-Itests', '-Isrc/app'], {cwd: root});
    assert.equal(execFileSync('nm', ['--defined-only', mqttObject], {encoding: 'utf8'}).trim(), '');

    const widgets = JSON.parse(fs.readFileSync(path.join(root, 'src/app/widgets/manifest.json'), 'utf8'));
    const expanded = execFileSync('python3', ['tools/widget_manifest.py', '--bundle', 'src/app/web/portal.js.bundle'],
        {cwd: root, encoding: 'utf8'});
    const modules = expanded.split('\n').filter(line => /^portal_widget_/.test(line) && line !== 'portal_widget_editor.js');
    assert.equal(modules.length, new Set(modules).size, 'Shared widget modules must execute once');
    assert.equal(modules.length, new Set(widgets.map(widget => widget.portal.js)).size);

    const profiles = [
        ['esp32c3-withsensors', 'ESP32C3', false],
        ['esp32c3-withsensors', 'ESP32C3', false, ['HAS_BUTTON=1', 'HAS_MQTT=1']],
        ['esp32-p4-lcd4b', 'ESP32P4', true],
        ['esp32-p4-lcd4b', 'ESP32P4', true, ['HAS_FACE_WIDGET=0']],
        ['esp32-p4-lcd4b', 'ESP32P4', true, ['HAS_EXTERNAL_DATA=0']],
        ['esp32-p4-lcd4b', 'ESP32P4', true, ['HAS_BLE=0', 'HAS_BLE_HID=0']],
        ['esp32-p4-lcd4b', 'ESP32P4', true, ['HAS_MICROPHONE_STREAMING=0']],
        ['esp32-p4-lcd4b', 'ESP32P4', false],
        ['esp32-p4-lcd4b', 'ESP32P4', false, ['HAS_MQTT=0', 'HAS_HA_LIVE=0']],
        ['esp32-p4-lcd4b', 'ESP32P4', true, ['HAS_MQTT=0', 'HAS_HA_LIVE=1']],
        ['esp32-p4-lcd4b', 'ESP32P4', true, ['HAS_CAMERA=0', 'HAS_NATIVE_EXTENSIONS=0', 'HAS_TOUCH=0', 'HAS_SOUND_PLAYER=0', 'HAS_IMAGE_LIBRARY=0', 'HAS_BUTTON=0']],
        ['reterminal-e1003-frame', 'ESP32S3', true],
        ['reterminal-sticky-interactive', 'ESP32S3', true],
        ['reterminal-sticky-interactive-sd', 'ESP32S3', true],
        ['m5stack-stopwatch', 'ESP32S3', true],
        ['jc3248w535', 'ESP32S3', true],
        ['jc4880p433-shutter', 'ESP32P4', true],
        ['esp32-p4-lcd4b', 'ESP32P4', true, ['ALARM_ENABLED=0', 'HAS_USB_HID=0', 'HAS_BLE_HID=0']]
    ];
    for (const [index, [board, target, psram, overrides = []]] of profiles.entries()) {
        const output = path.join(directory, String(index));
        const compiler = ['c++', '-Isrc/app', '-Itests', '-Isrc/boards/' + board, '-DBOARD_HAS_OVERRIDE',
            '-DCONFIG_IDF_TARGET_' + target, ...(psram ? ['-DBOARD_HAS_PSRAM'] : []),
            ...overrides.map(flag => '-D' + flag)];
        if (overrides.length) {
            const overrideHeader = path.join(directory, 'overrides-' + index + '.h');
            fs.writeFileSync(overrideHeader, '#include "' + path.join(root, 'src/app/board_config.h') + '"\n' +
                overrides.map(flag => {
                    const [name, value] = flag.split('=');
                    return '#undef ' + name + '\n#define ' + name + ' ' + value + '\n';
                }).join(''));
            compiler.push('-include', overrideHeader);
        }
        execFileSync('python3', ['tools/portal_assets.py', 'esp32-macropad', 'ESP32 Macropad',
            '--cpp-command', JSON.stringify(compiler), '--output-dir', output], {cwd: root, stdio: 'pipe'});
        const header = fs.readFileSync(path.join(output, 'web_assets.h'), 'utf8');
        const {conditions, sizes} = JSON.parse(fs.readFileSync(path.join(output, 'portal-assets.json')));
        const selected = execFileSync(compiler[0], [...compiler.slice(1), '-I' + output,
            '-E', '-P', '-x', 'c++', path.join(output, 'web_assets.h')],
        {cwd: root, encoding: 'utf8', maxBuffer: 8 * 1024 * 1024, stdio: 'pipe'});
        const assets = new Map();
        for (const match of selected.matchAll(/const uint8_t (\w+_gz)\[\] PROGMEM = \{([\s\S]*?)\};/g)) {
            assert(!assets.has(match[1]), 'Duplicate selected asset: ' + match[1]);
            const bytes = Buffer.from([...match[2].matchAll(/0x([0-9a-f]{2})/g)].map(value => parseInt(value[1], 16)));
            assert.equal(bytes.length, sizes[match[1]]);
            assets.set(match[1], zlib.gunzipSync(bytes).toString());
        }
        assert.equal((header.match(/const uint8_t portal_js_gz\[/g) || []).length, 1,
            'Generate exactly one bundle, not a combination matrix');
        for (const [name, text] of assets) if (name.endsWith('_js_gz')) new vm.Script(text, {filename: board + '/' + name});
        const js = assets.get('portal_js_gz');
        const css = assets.get('portal_all_css_gz');
        const shell = assets.get('shell_html_gz');
        const pad = assets.get('pad_editor_fragment_html_gz') || '';
        assert(js && css && shell);
        for (const callback of ['uploadFirmware', 'saveFragmentConfig', 'init_wifi_fragment']) assert(js.includes(callback));
        for (const [gate, marker] of [
            ['HAS_DISPLAY', 'init_screen_preview_fragment'],
            ['HAS_DISPLAY', 'init_pad_editor_fragment'],
            ['HAS_DISPLAY', 'function actionEditorPopulateScreens('],
            ['HAS_AUDIO', 'init_volume_fragment'],
            ['HAS_AUDIO', 'updateAmplifierSettings'],
            ['HAS_MQTT', 'init_mqtt_fragment'],
            ['HAS_BUTTON', 'HW_BUTTON_PREFIXES'],
            ['HAS_MQTT && (HAS_DISPLAY || HAS_BUTTON)', 'MQTT_TRIGGER_PREFIXES'],
            ['HAS_MCP', 'init_mcp_fragment'],
            ['HAS_SOUND_PLAYER', 'function loadSoundList('],
            ['HAS_HA_LIVE', 'function haBindingBuilderHTML('],
            ['HAS_NATIVE_EXTENSIONS', 'init_extensions_fragment'],
            ['HAS_IMAGE_LIBRARY', 'init_image_library_fragment'],
            ['IS_COFFEE_SCALE', 'function actionEditorLoadBrewTemplates(']
        ]) {
            // Some modules require an action-editor host as well as their owner.
            const enabled = conditions[gate] && (gate !== 'IS_COFFEE_SCALE' || conditions['HAS_DISPLAY || HAS_BUTTON']);
            assert.equal(js.includes(marker), !!enabled, board + '/' + marker);
        }
        for (const widget of widgets) {
            const gate = [...widget.gates, ...(widget.any_gates ? ['(' + widget.any_gates.join(' || ') + ')'] : [])].join(' && ');
            const enabled = conditions[gate];
            assert.equal(pad.includes('id="pad-edit-' + widget.type + '-widget-option"'), enabled, board + '/' + widget.type);
        }
        for (const [gate, symbol] of [
            ['ALARM_ENABLED', 'portal_alarms_js_gz'],
            ['HAS_EXTERNAL_DATA', 'portal_external_data_js_gz'],
            ['HAS_MICROPHONE_STREAMING', 'portal_live_microphone_js_gz'],
            ['HAS_AUDIO_INPUT', 'portal_microphone_js_gz'],
            ['HAS_VOICE_NOTES', 'portal_voice_notes_js_gz'],
            ['HAS_TTF_FONTS', 'portal_fonts_js_gz'],
            ['HAS_REMOTE_LOG', 'portal_logs_js_gz'],
            ['HAS_AUTO_SHUTDOWN', 'portal_power_savings_js_gz'],
            ['HAS_BLE || HAS_BLE_HID', 'portal_bluetooth_js_gz']
        ]) assert.equal(assets.has(symbol), conditions[gate], board + '/' + symbol);
        for (const [gate, symbol] of [
            ['ALARM_ENABLED', 'alarm_schedule_fragment_html_gz'],
            ['ALARM_ENABLED', 'alarm_behavior_fragment_html_gz'],
            ['HAS_EXTERNAL_DATA', 'external_data_fragment_html_gz'],
            ['HAS_VOICE_NOTES', 'voice_notes_fragment_html_gz'],
            ['HAS_VOICE_NOTES', 'voice_notes_settings_fragment_html_gz'],
            ['HAS_AUDIO_INPUT', 'microphone_fragment_html_gz'],
            ['HAS_TTF_FONTS', 'fonts_fragment_html_gz'],
            ['HAS_BLE || HAS_BLE_HID', 'bluetooth_fragment_html_gz']
        ]) {
            assert.equal(assets.has(symbol), conditions[gate], board + '/' + symbol);
            const id = symbol.replace(/_fragment_html_gz$/, '').replaceAll('_', '-');
            assert.equal(selected.includes('{"' + id + '", ' + symbol), conditions[gate],
                'Fragment dispatch table must contain only selected assets');
        }
        for (const [marker, enabled] of [
            ['function showBindingHelp(', conditions.HAS_DISPLAY],
            ['function bindingValidateHomeConfig(', conditions.HAS_DISPLAY],
            ['function padButtonBuilderOpen(', conditions.HAS_DISPLAY],
            ['function bindingBuilderToken(', conditions.HAS_DISPLAY],
            ['function showBindingBuilder(', conditions.HAS_DISPLAY],
            ['function bindingBuilderFormatControlsHTML(', conditions.HAS_DISPLAY],
            ['const catalogBindingProvider=', conditions.HAS_DISPLAY],
            ['const mqttBindingProvider=', conditions['HAS_MQTT && HAS_DISPLAY']],
            ['mqttButtonBuilderProvider=', conditions['HAS_MQTT && HAS_DISPLAY']],
            ['catalogButtonBuilderProvider=', conditions.HAS_DISPLAY],
            ['const haButtonBuilderProvider=', conditions.HAS_HA_LIVE]
        ]) assert.equal(js.includes(marker), enabled, board + '/' + marker);
        if (conditions.HAS_DISPLAY) {
            const shared = js.indexOf('const BUTTON_BUILDER_PROVIDERS=');
            const templates = js.indexOf('const padButtonBuilders=');
            const dialog = js.indexOf('function padButtonBuilderOpen(');
            assert(shared >= 0 && shared < templates && templates < dialog);
            for (const provider of ['catalogButtonBuilderProvider=', 'mqttButtonBuilderProvider=', 'const haButtonBuilderProvider=']) {
                if (js.includes(provider)) assert(shared < js.indexOf(provider) && js.indexOf(provider) < templates);
            }
        }
        assert(!js.includes('externalDataBuilderProvider'));
        if (conditions.HAS_EXTERNAL_DATA) assert(assets.get('portal_external_data_js_gz').includes('externalDataBuilderProvider'));
        if (conditions.HAS_TTF_FONTS) {
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
        assert.equal(shell.includes('src="/portal_power_savings.js'), conditions.HAS_AUTO_SHUTDOWN);
        assert.equal(shell.includes('id="binding-help-overlay"'), conditions.HAS_DISPLAY);
        assert.equal(shell.includes('src="/portal-live-microphone.js'), conditions.HAS_MICROPHONE_STREAMING);
        assert.equal(css.includes('.binding-docs-modal'), conditions.HAS_DISPLAY);
        assert(!js.includes('portalStreamFetch'), 'Standalone microphone transport leaked into shared bundle');
        assert(!js.includes('init_bluetooth_fragment'), 'Standalone Bluetooth script leaked into shared bundle');
        if (!conditions.HAS_DISPLAY) assert(!js.includes('const padWidgetEditors='));
        if (board === 'reterminal-sticky-interactive' || overrides.includes('HAS_CAMERA=0')) {
            for (const marker of ['widget_camera_scale', 'extension_tick_interval_ms', 'widget_waveform_sensor',
                'function actionEditorGamepadHTML(']) assert(!js.includes(marker), board + '/' + marker);
        }
        if (!conditions.HAS_MQTT) assert(!js.includes('function initMqttTriggers('));
        if (!conditions.HAS_SOUND_PLAYER) assert(!js.includes('function actionEditorLoadOperationSounds('));
        if (!conditions.HAS_IMAGE_LIBRARY) assert(!js.includes('/api/images?directory='));
        if (!conditions.HAS_EPAPER_PANEL) assert(!js.includes('epaper_bw_conversion'));
        if (index === 0) {
            assert.throws(() => execFileSync(compiler[0], [...compiler.slice(1), '-DALARM_ENABLED=1', '-I' + output,
                '-E', '-P', '-x', 'c++', path.join(output, 'web_assets.h')], {cwd: root, stdio: 'pipe'}),
            error => error.stderr.toString().includes('Portal assets do not match firmware configuration'));
        }
        console.log('PASS: selected payloads ' + board + ' ' + overrides.join(',') + ' (portal.js=' + sizes.portal_js_gz + ')');
    }
} finally {
    fs.rmSync(directory, {recursive: true, force: true});
}
