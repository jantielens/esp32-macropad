const assert = require('node:assert/strict');
const {execFileSync} = require('node:child_process');
const path = require('node:path');
const vm = require('node:vm');
const zlib = require('node:zlib');

const root = path.resolve(__dirname, '..');
execFileSync('bash', ['tools/minify-web-assets.sh', 'esp32-macropad', 'ESP32 Macropad'],
    {cwd: root, stdio: 'pipe'});

for (const [board, target, psram] of [['esp32c3-withsensors', 'ESP32C3', false], ['esp32-p4-lcd4b', 'ESP32P4', true], ['esp32-p4-lcd4b', 'ESP32P4', false]]) {
    const source = execFileSync('c++', ['-E', '-P', '-x', 'c++', 'src/app/web_assets.h',
        '-Itests', '-Isrc/boards/' + board, '-DBOARD_HAS_OVERRIDE', '-DCONFIG_IDF_TARGET_' + target,
        ...(psram ? ['-DBOARD_HAS_PSRAM'] : [])],
        {cwd: root, encoding: 'utf8', maxBuffer: 8 * 1024 * 1024, stdio: ['ignore', 'pipe', 'pipe']});
    const assets = new Map();
    const pattern = /const uint8_t (\w+_gz)\[\] PROGMEM = \{([\s\S]*?)\};/g;
    for (const match of source.matchAll(pattern)) {
        assert(!assets.has(match[1]), 'Duplicate selected asset: ' + match[1]);
        const bytes = Buffer.from([...match[2].matchAll(/0x([0-9a-f]{2})/g)].map(value => parseInt(value[1], 16)));
        assets.set(match[1], zlib.gunzipSync(bytes).toString());
    }
    assert(assets.has('portal_js_gz'));
    for (const [name, text] of assets) {
        if (name.endsWith('_js_gz')) new vm.Script(text, {filename: board + '/' + name});
    }
    const js = assets.get('portal_js_gz');
    assert(js.includes('function uploadFirmware('), 'HTML upload callback was removed');
    assert(js.includes('function saveFragmentConfig('), 'Fragment save callback was removed');
    const display = board === 'esp32-p4-lcd4b';
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
    for (const stem of ['mcp', 'recipes', 'recipe_catalog', 'extensions']) {
        assert.equal(assets.has(stem + '_fragment_html_gz'), display, stem);
    }
    if (!display) {
        for (const stem of ['music', 'camera_motion', 'mqtt_triggers', 'hw_buttons', 'feedback', 'storage']) {
            assert(!assets.has(stem + '_fragment_html_gz'), 'Headless-unused asset: ' + stem);
        }
    }
    console.log('PASS: selected, valid minified assets for ' + board);
}