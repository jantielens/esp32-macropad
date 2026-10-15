const padWidgetEditors = new Map();

function padRegisterWidgetEditor(editor) {
    if (padWidgetEditors.has(editor.type)) throw new Error('Duplicate widget editor: ' + editor.type);
    padWidgetEditors.set(editor.type, editor);
}

function padWidgetEditor(type) {
    return padWidgetEditors.get(type);
}

function padWidgetEditorsLoad(button) {
    padWidgetEditors.forEach(editor => {
        const config = button.widget_type === editor.type ? button : {};
        if (editor.load) editor.load(config);
        editor.fields.forEach(field => {
            const input = document.getElementById(field.id);
            const fallback = typeof field.default === 'function' ? field.default() : field.default;
            const value = config[field.key] === undefined ? fallback : config[field.key];
            if (field.kind === 'checkbox') input.checked = value;
            else if (field.kind === 'color') padSetBindableColor(field.id, value, fallback);
            else input.value = field.kind === 'label' ? padLabelToInput(value) : String(value);
        });
        (editor.groups || []).forEach(group => { group.element.open = group.open; });
    });
}

function padWidgetEditorBuild(button, editor) {
    editor.fields.forEach(field => {
        const input = document.getElementById(field.id);
        padWidgetValidateField(input, field);
        let value;
        if (field.kind === 'checkbox') value = input.checked;
        else if (field.kind === 'boolean-select') value = input.value === 'true';
        else if (field.kind === 'color') value = padGetBindableColor(field.id);
        else if (field.kind === 'label') value = padLabelFromInput(field.id);
        else if (field.kind === 'number') value = input.value.trim() === '' ? field.default : Number(input.value);
        else if (field.kind === 'bindable-number') value = padGetBindableNumber(field.id, field.default);
        else value = input.value.trim();
        if (value !== '') button[field.key] = value;
    });
}

function padWidgetValidateField(input, field) {
    const raw = String(input.value).trim();
    let error = input.validity && !input.validity.valid ? input.validationMessage : '';
    if (input.maxLength > 0 && raw.length > input.maxLength) error = 'Maximum length is ' + input.maxLength;
    if ((field.kind === 'bindable-number' || field.kind === 'numeric-text') && raw && !raw.includes('[') &&
        (!Number.isFinite(Number(raw)) || Number(raw) !== parseFloat(raw))) {
        error = 'Enter a number or binding';
    }
    if (field.binding && typeof bindingValidateInput === 'function' && !bindingValidateInput(input, field.binding)) {
        error = 'Invalid binding';
    }
    if (error) {
        const label = document.querySelector && document.querySelector('label[for="' + input.id + '"]');
        throw Object.assign(new Error((label ? label.textContent.trim() : input.id) + ': ' + error), { fieldIds: [input.id] });
    }
}

function padWidgetEditorsInit(info) {
    padWidgetEditors.forEach(editor => {
        editor.supported = !editor.available || editor.available(info);
        if (editor.optionId) {
            const option = document.getElementById(editor.optionId);
            option.hidden = !editor.supported;
            option.disabled = !editor.supported;
        }
        if (editor.init) editor.init();
        editor.groups = editor.sections.flatMap(section => {
            const root = document.getElementById(section.id);
            const groups = Array.from(root.querySelectorAll('details'));
            if (root.tagName === 'DETAILS') groups.unshift(root);
            return groups.map(element => ({ element, open: element.open }));
        });
        editor.fields.forEach(field => {
            if (field.binding) bindingAttachValidation(document.getElementById(field.id), field.binding);
        });
    });
}

function padWidgetEditorsDispose() {
    padWidgetEditors.forEach(editor => { editor.groups = []; });
}

function padWidgetEditorsRefresh(type) {
    padWidgetEditors.forEach(editor => {
        editor.sections.forEach(section => {
            const element = document.getElementById(section.id);
            element.dataset.buttonTab = section.tab;
            element.style.display = type === editor.type && editor.supported !== false ? '' : 'none';
        });
    });
}