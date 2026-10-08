const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

const context = { console, Blob, Set };
context.window = context;
vm.createContext(context);
vm.runInContext(
    fs.readFileSync('src/app/web/portal_recipes.js', 'utf8') +
    '\nthis.recipeLoadCatalogJson = recipeLoadCatalogJson; this.recipeExpand = recipeExpand; this.recipeResolveLayout = recipeResolveLayout; this.recipeFindPlacement = recipeFindPlacement; this.recipeFindMinimumResize = recipeFindMinimumResize; this.recipeDeepMerge = recipeDeepMerge; this.recipeMergeBindings = recipeMergeBindings; this.recipeImpactSummary = recipeImpactSummary; this.recipePlacementSummary = recipePlacementSummary;',
    context
);

const fixture = fs.readFileSync('docs/samples/recipe-catalog.json', 'utf8');
const catalog = context.recipeLoadCatalogJson(fixture);
assert.strictEqual(catalog.recipes.length, 3);
assert.strictEqual(catalog.recipes[0].id, 'pomodoro');
assert.strictEqual(catalog.recipes[0].layout.buttons.length, 1);
assert.strictEqual(catalog.recipes[0].layout.flow, 'row');
assert.strictEqual(catalog.recipes[0].layout.buttons[0].size, 'M');
assert.strictEqual(catalog.recipes[0].layout.buttons[0].shape, 'wide');
assert.strictEqual(catalog.recipes[0].layout.buttons[0].label_center, '[timer:1;mm:ss]');
assert.strictEqual(catalog.recipes[0].layout.buttons[0].icon_id, 'mi_timer');
assert.strictEqual(catalog.recipes[0].layout.buttons[0].icon_scale_pct, 50);
assert.strictEqual(catalog.recipes[0].layout.buttons[0].icon_position, 'left');
assert.strictEqual(catalog.recipes[0].layout.buttons[0].actions[0].timer_command, 'toggle');
assert.strictEqual(catalog.recipes[0].layout.buttons[0].lp_actions[0].timer_command, 'reset');
assert.strictEqual(catalog.recipes[0].provision.components.timers['1'].expire_actions[0].type, 'sound_alert');
assert.strictEqual(catalog.recipes[0].provision.components.timers['1'].expire_actions[0].sound_alert_pattern, '880:200;0:100;880:200');
assert.strictEqual(catalog.recipes[0].provision.components.timers['1'].expire_actions[1].notify_text, 'Focus session complete');

const energyRecipe = catalog.recipes.find(recipe => recipe.id === 'home-energy');
const energy = context.recipeExpand(energyRecipe, {
    grid_power: '[mqtt:grid]', solar_power: '[mqtt:solar]',
    grid_history_entity: 'sensor.grid_history', solar_history_entity: 'sensor.solar_history',
    home_history_entity: 'sensor.home_history',
});
assert.strictEqual(energy.provision.pad.bindings.grid, '[mqtt:grid]');
assert.strictEqual(energy.provision.pad.bindings.solar, '[mqtt:solar]');
assert.strictEqual(energy.layout.buttons[4].widget_type, 'sparkline');
assert.strictEqual(energy.layout.buttons[4].widget_sparkline_ha_entity_3, 'sensor.home_history');
assert.strictEqual(energy.provision.pad.bindings.home, '[expr:[mqtt:grid] + [mqtt:solar]]');
assert.strictEqual(energyRecipe.parameters[0].description, 'MQTT value for imported and exported grid power in kW.');
assert.strictEqual(context.recipeImpactSummary(energyRecipe), 'Adds 5 buttons | needs a 3x4 free area | adds 3 named bindings');
const clockRecipe = catalog.recipes.find(recipe => recipe.id === 'alarm-clock');
assert.strictEqual(clockRecipe.layout.buttons[0].fg_color, '333333');
assert.strictEqual(clockRecipe.layout.buttons[2].confirm, true);
assert.strictEqual(clockRecipe.layout.buttons[2].label_bottom, 'Dismiss alarm');
assert.strictEqual(clockRecipe.provision.components.alarms['1'].enabled, false);
assert.strictEqual(clockRecipe.provision.components.alarms['1'].hour, undefined);
assert.strictEqual(context.recipeExpand(clockRecipe, { target_pad: 'pad_12' }).provision.components.alarms['1'].on_ring[1].target, 'pad_12');
assert.strictEqual(clockRecipe.post_install.links[0].fragment, 'alarm-schedule');
context.deviceInfoCache = { has_alarm: true, has_audio: true, has_sound_player: true };
assert.strictEqual(context.recipeSupports(clockRecipe), true);
context.deviceInfoCache.has_alarm = false;
assert.strictEqual(context.recipeSupports(clockRecipe), false);
context.deviceInfoCache.has_alarm = true;
context.deviceInfoCache.has_sound_player = false;
assert.strictEqual(context.recipeSupports(clockRecipe), false);
assert.strictEqual(context.recipePlacementSummary([
    { col: 1, row: 2, col_span: 3, row_span: 2 }, { col: 4, row: 5, col_span: 1, row_span: 1 },
]), 'Adds 2 buttons in columns 2-5, rows 3-6.');

const adaptive = context.recipeResolveLayout({
    flow: 'row',
    buttons: [{ size: 'M', shape: 'square' }, { size: 'L', shape: 'very_tall' }],
}, { cols: 4, rows: 8, buttons: [] }, 0, 0, { button_w: 150, button_h: 100, gap: 0 });
assert.deepStrictEqual(JSON.parse(JSON.stringify(adaptive.map(function (button) {
    return { col: button.col, row: button.row, col_span: button.col_span, row_span: button.row_span };
}))), [
    { col: 0, row: 0, col_span: 2, row_span: 3 },
    { col: 2, row: 0, col_span: 1, row_span: 5 },
]);

const edgeAnchored = context.recipeResolveLayout({
    flow: 'row', buttons: [{ size: 'M', shape: 'wide' }],
}, { cols: 6, rows: 8, buttons: [] }, 5, 7, { button_w: 100, button_h: 100, gap: 0 });
assert.deepStrictEqual(JSON.parse(JSON.stringify(edgeAnchored.map(function (button) {
    return { col: button.col, row: button.row, col_span: button.col_span, row_span: button.row_span };
}))), [
    { col: 5, row: 7, col_span: 1, row_span: 1 },
]);

const naturallyWide = context.recipeResolveLayout({
    flow: 'row', buttons: [{ size: 'M', shape: 'wide' }],
}, { cols: 2, rows: 6, buttons: [] }, 0, 2, { button_w: 150, button_h: 90, gap: 0 });
assert.deepStrictEqual(JSON.parse(JSON.stringify(naturallyWide.map(function (button) {
    return { col: button.col, row: button.row, col_span: button.col_span, row_span: button.row_span };
}))), [
    { col: 0, row: 2, col_span: 1, row_span: 1 },
]);

const oneButtonLayout = {
    buttons: [{ col_offset: 0, row_offset: 0, label_center: 'Recipe' }],
};
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.recipeFindPlacement(
    oneButtonLayout,
    { cols: 2, rows: 1, buttons: [{ col: 0, row: 0, col_span: 1, row_span: 1 }] },
    { button_w: 100, button_h: 100, gap: 0 }
))), { col: 1, row: 0 });

assert.deepStrictEqual(JSON.parse(JSON.stringify(context.recipeDeepMerge(
    { appearance: { color: '#000000', labels: ['old'] }, enabled: true },
    { appearance: { labels: ['new'], border: 2 }, delay_ms: 300 }
))), {
    appearance: { color: '#000000', labels: ['new'], border: 2 }, enabled: true, delay_ms: 300,
});
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.recipeMergeBindings(
    { grid: '[mqtt:grid]' }, { grid: '[mqtt:grid]', solar: '[mqtt:solar]' }
))), { grid: '[mqtt:grid]', solar: '[mqtt:solar]' });
assert.throws(
    () => context.recipeMergeBindings({ grid: '[mqtt:old]' }, { grid: '[mqtt:new]' }),
    /grid.*different value/
);

assert.throws(
    () => context.recipeLoadCatalogJson('{"schema":1,"catalog_version":"x","recipes":[{"id":"bad","name":"Bad","description":"Bad"}]}'),
    /layout.buttons/
);
assert.throws(
    () => context.recipeLoadCatalogJson('{"schema":2,"catalog_version":"x","recipes":[]}'),
    /schema/
);
assert.throws(
    () => context.recipeLoadCatalogJson('{"schema":1,"catalog_version":"x","recipes":[{"id":"bad","name":"Bad","description":"Bad","parameters":[{"id":"value","label":"Value","description":1,"type":"string","default":"x"}],"layout":{"buttons":[{"col_offset":0,"row_offset":0}]}}]}'),
    /parameter/
);

const alarmRecipe = {
    id: 'alarm-clock', name: 'Alarm Clock', description: 'Clock with alarm controls.',
    requires: ['display', 'alarm', 'sound_player'],
    confirmation: 'Replace Alarm 1 actions and disable its schedule?',
    post_install: { message: 'Configure your alarm.', links: [{ label: 'Open alarm schedule', fragment: 'alarm-schedule' }] },
    layout: { buttons: [{ col_offset: 0, row_offset: 0 }] },
    provision: { components: { alarms: { '1': { enabled: false, on_ring: [{ type: 'screen', target: '${target_pad}' }] } } } },
};
const alarmCatalog = { schema: 1, catalog_version: 'test', recipes: [alarmRecipe] };
assert.doesNotThrow(() => context.recipeLoadCatalogJson(JSON.stringify(alarmCatalog)));
assert.strictEqual(context.recipeExpand(alarmRecipe, { target_pad: 'pad_7' }).provision.components.alarms['1'].on_ring[0].target, 'pad_7');
alarmRecipe.parameters = [{ id: 'target_pad', label: 'Target', type: 'string', default: 'pad_1' }];
assert.throws(() => context.recipeLoadCatalogJson(JSON.stringify(alarmCatalog)), /reserved/);
delete alarmRecipe.parameters;
alarmRecipe.post_install.links[0].fragment = 'javascript:alert(1)';
assert.throws(() => context.recipeLoadCatalogJson(JSON.stringify(alarmCatalog)), /fragment links/);
alarmRecipe.post_install.links[0].fragment = 'alarm-schedule';
alarmRecipe.confirmation = '';
assert.throws(() => context.recipeLoadCatalogJson(JSON.stringify(alarmCatalog)), /confirmation/);

context.deviceInfoCache = { max_grid_cols: 4, max_grid_rows: 4 };
context.padGetButtonSizes = async function () { return { button_w: 100, button_h: 100, gap: 0 }; };
context.recipeFindMinimumResize({
    buttons: [{ col_offset: 1, row_offset: 0, label_center: 'Recipe' }],
}, { cols: 1, rows: 1, buttons: [] }).then(function (resize) {
    assert.deepStrictEqual(JSON.parse(JSON.stringify({ cols: resize.cols, rows: resize.rows })), { cols: 2, rows: 1 });
    console.log('portal_recipes: PASS');
}).catch(function (error) {
    console.error(error);
    process.exitCode = 1;
});

async function testInstallation() {
    for (const outcome of ['success', 'cancel', 'pad-failure', 'component-failure', 'unsupported']) {
        const elements = new Map();
        function element() {
            return { hidden: true, children: [], classList: { add() {} }, appendChild(child) { this.children.push(child); } };
        }
        const writes = [];
        const messages = [];
        const install = {
            console, Blob, Set, deviceInfoCache: { has_alarm: outcome !== 'unsupported', has_sound_player: true },
            confirm: () => outcome !== 'cancel',
            showMessage: (message) => messages.push(message),
            padUploadPageIcons: async () => {},
            document: {
                getElementById(id) { if (!elements.has(id)) elements.set(id, element()); return elements.get(id); },
                createElement: element,
            },
            fetch: async (url, options) => {
                if (options && options.method === 'POST') {
                    writes.push({ url, body: JSON.parse(options.body) });
                    const failed = outcome === 'pad-failure' && url.startsWith('/api/pad?') ||
                        outcome === 'component-failure' && url.includes('/component/');
                    return { ok: !failed, status: failed ? 503 : 200, json: async () => ({ error: 'Save failed' }) };
                }
                return { ok: true, json: async () => ({ lateness_minutes: 360, '1': { enabled: true, hour: 8, minute: 30, weekdays: 62, snooze_minutes: 9, on_stop: [] } }) };
            },
        };
        install.window = install;
        vm.createContext(install);
        vm.runInContext(fs.readFileSync('src/app/web/portal_recipes.js', 'utf8') + '\nthis.state = recipeState;', install);
        const loadPad = install.recipeLoadPad;
        install.recipeLoadPad = async () => {};
        install.state.selected = JSON.parse(JSON.stringify(alarmCatalog.recipes[0]));
        install.state.selected.confirmation = 'Replace Alarm 1 actions and disable its schedule?';
        install.state.pad = { page: 6, cols: 2, rows: 2, buttons: [] };
        install.state.placement = { col: 0, row: 0 };
        install.state.buttonSizes = { button_w: 100, button_h: 100, gap: 0 };
        await Promise.all([install.recipeInstall(), install.recipeInstall()]);
        assert.strictEqual(install.state.installing, false);
        if (outcome === 'cancel' || outcome === 'unsupported') {
            assert.strictEqual(writes.length, 0);
        } else {
            assert.strictEqual(writes[0].url, '/api/pad?page=6');
            assert.strictEqual(writes.length, outcome === 'pad-failure' ? 1 : 2);
        }
        if (outcome === 'success') {
            assert.strictEqual(writes[1].body['1'].enabled, false);
            assert.strictEqual(writes[1].body['1'].hour, 8);
            assert.strictEqual(writes[1].body['1'].on_ring[0].target, 'pad_6');
            assert.strictEqual(install.state.installationComplete, true);
            assert.strictEqual(elements.get('recipe-followup').hidden, false);
            assert.strictEqual(elements.get('recipe-followup-links').children[0].href, '#alarm-schedule');
            let resolveSlow;
            install.fetch = async url => {
                if (url === '/api/pad?page=0') return await new Promise(resolve => { resolveSlow = resolve; });
                return { ok:true, json:async () => ({cols:2,rows:2,buttons:[]}) };
            };
            install.padGetButtonSizes = async () => ({button_w:100,button_h:100,gap:0});
            install.recipeRenderPlacement = () => {};
            const select = install.document.getElementById('recipe-pad-select');
            select.value = '0';
            const slowLoad = loadPad();
            select.value = '7';
            await loadPad();
            resolveSlow({ok:true,json:async () => ({cols:3,rows:3,buttons:[]})});
            await slowLoad;
            assert.strictEqual(install.state.pad.page, 7);
            assert.strictEqual(install.state.pad.cols, 2);
            assert.strictEqual(elements.get('recipe-followup').hidden, true);
        } else {
            assert.notStrictEqual(install.state.installationComplete, true);
            assert(!messages.some(message => message.endsWith(' installed')));
            if (outcome === 'component-failure') assert(messages.some(message => message.includes('pad was saved')));
        }
    }
    console.log('recipe installation: PASS (success, cancellation, unsupported device, pad and component failures)');
}
testInstallation().catch(error => { console.error(error); process.exitCode = 1; });