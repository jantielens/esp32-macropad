const padWidgetEditors = new Map();

function padRegisterWidgetEditor(editor) {
    if (padWidgetEditors.has(editor.type)) throw new Error('Duplicate widget editor: ' + editor.type);
    padWidgetEditors.set(editor.type, editor);
}

function padWidgetEditor(type) {
    return padWidgetEditors.get(type);
}

function padWidgetSetGroupColor(groupId, field, resolved) {
    const raw = document.getElementById(field.id).value;
    const fallback = typeof field.default === 'function' ? field.default() : field.default;
    const color = padColorNormalizeLiteral(raw) || (raw.includes('[') && padColorNormalizeLiteral(resolved)) ||
        padColorNormalizeLiteral(fallback);
    document.getElementById(groupId).style.setProperty('--pad-source-color', color || 'var(--portal-border-light)');
}

function padWidgetActionPrefixes() {
    return Array.from(padWidgetEditors.values()).flatMap(editor => editor.actions.prefixes || []);
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
            if (field.preview) field.preview();
        });
        (editor.groups || []).forEach(group => {
            const settings = editor.fields.filter(field => group.element.contains(document.getElementById(field.id)))
                .concat((editor.disclosureSettings || []).filter(setting => setting.group === group.element.id));
            group.element.open = group.open || settings.some(field => {
                const value = config[field.key];
                if (value === undefined || value === null || String(value).trim() === '') return false;
                const fallback = typeof field.default === 'function' ? field.default() : field.default;
                if (typeof fallback === 'number') return Number(value) !== fallback;
                const normalize = value => field.kind === 'color' ? String(value).trim().toLowerCase() : String(value).trim();
                return normalize(value) !== normalize(fallback);
            });
        });
        (editor.groups || []).forEach(group => {
            if (!group.element.open) return;
            (editor.groups || []).forEach(parent => {
                if (parent.element.contains(group.element)) parent.element.open = true;
            });
        });
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
    if (editor.build) editor.build(button);
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

function padWidgetEditorsInit(info, onSettled = () => {}) {
    padWidgetEditors.forEach(editor => {
        editor.supported = !editor.available || editor.available(info);
        const option = document.getElementById('pad-edit-' + editor.type + '-widget-option');
        if (option) {
            option.hidden = !editor.supported;
            option.disabled = !editor.supported;
        }
        if (editor.init) editor.init(() => onSettled(editor.type));
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
    padWidgetEditors.forEach(editor => {
        if (editor.dispose) editor.dispose();
        editor.groups = [];
    });
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