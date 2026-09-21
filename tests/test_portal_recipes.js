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

const fixture = fs.readFileSync('tests/recipe_catalog_fixture.json', 'utf8');
const catalog = context.recipeLoadCatalogJson(fixture);
assert.strictEqual(catalog.recipes.length, 2);
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

const energy = context.recipeExpand(catalog.recipes[1], {
    grid_power: '[mqtt:grid]', solar_power: '[mqtt:solar]',
    grid_history_entity: 'sensor.grid_history', solar_history_entity: 'sensor.solar_history',
    home_history_entity: 'sensor.home_history',
});
assert.strictEqual(energy.provision.pad.bindings.grid, '[mqtt:grid]');
assert.strictEqual(energy.provision.pad.bindings.solar, '[mqtt:solar]');
assert.strictEqual(energy.layout.buttons[4].widget_type, 'sparkline');
assert.strictEqual(energy.layout.buttons[4].widget_sparkline_ha_entity_3, 'sensor.home_history');
assert.strictEqual(energy.provision.pad.bindings.home, '[expr:[mqtt:grid] + [mqtt:solar]]');
assert.strictEqual(catalog.recipes[1].parameters[0].description, 'MQTT value for imported and exported grid power in kW.');
assert.strictEqual(context.recipeImpactSummary(catalog.recipes[1]), 'Adds 5 buttons | needs a 3x4 free area | adds 3 named bindings');
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