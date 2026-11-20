function actionEditorPopulateScreens(prefixes, screens) {
    if (!screens) return;
    var pads = screens.filter(function(screen) { return /^pad_\d+$/.test(screen.id); });
    var builtins = screens.filter(function(screen) { return !/^pad_\d+$/.test(screen.id); });
    pads.sort(function(first, second) { return Number(first.id.slice(4)) - Number(second.id.slice(4)); });
    function screenLabel(screen) {
        if (!/^pad_\d+$/.test(screen.id)) return screen.name || screen.id;
        var label = 'Pad ' + (Number(screen.id.slice(4)) + 1);
        return !screen.name || screen.name === label ? label :
            screen.name.indexOf(label + ': ') === 0 ? screen.name : label + ': ' + screen.name;
    }
    prefixes.forEach(function(prefix) {
        actionEditorCatalog().forEach(function(entry) {
            (entry.operations || []).forEach(function(operation) {
                (operation.arguments || []).forEach(function(argument) {
                    if (argument.control !== 'screens') return;
                    var base = prefix + '-op-' + operation.id + '-' + argument.name;
                    var operationSelect = document.getElementById(base + '-fixed');
                    if (!operationSelect) return;
                    var operationSelected = operationSelect.getAttribute('data-pending-value') || operationSelect.value;
                    var availableScreens = pads.concat(builtins);
                    while (operationSelect.options.length) operationSelect.remove(0);
                    availableScreens.forEach(function(screen) {
                        var option = document.createElement('option');
                        option.value = screen.id;
                        option.textContent = screenLabel(screen);
                        operationSelect.appendChild(option);
                    });
                    if (operationSelected && !availableScreens.some(function(screen) { return screen.id === operationSelected; })) {
                        var missing = document.createElement('option');
                        missing.value = operationSelected;
                        missing.textContent = operationSelected + ' (unavailable)';
                        operationSelect.appendChild(missing);
                    }
                    if (!availableScreens.length) {
                        var empty = document.createElement('option');
                        empty.value = '';
                        empty.textContent = 'No screens available';
                        operationSelect.appendChild(empty);
                    }
                    if (operationSelected) operationSelect.value = operationSelected;
                    else if (availableScreens.length) operationSelect.value = availableScreens[0].id;
                    operationSelect.removeAttribute('data-pending-value');
                    operationSelect.setAttribute('data-screens-loaded', '1');
                    var template = document.getElementById(base + '-template');
                    if (template && template.hasAttribute('data-dynamic-waiting')) {
                        if (!template.value && operationSelect.value) {
                            template.value = operationSelect.value;
                            template.setAttribute('data-dynamic-initialized', '1');
                        }
                        template.removeAttribute('data-dynamic-waiting');
                    }
                });
            });
        });
    });
}
