function padPasteFields(button) {
    const fields = PAD_PV_FIELDS.map(field => ({
        key: field.field, id: field.id, kind: field.kind,
        label: /^label_/.test(field.field)
    }));
    for (const slot of ['top', 'center', 'bottom']) {
        fields.push({ key: 'label_' + slot + '_style', kind: 'style', maxBytes: 127 });
    }
    fields.push(
        { key: 'bg_image_path', maxBytes: 127 },
        { key: 'bg_image_url', maxBytes: 255 },
        { key: 'bg_pattern_color', kind: 'color' }
    );
    const editor = padWidgetEditor(button.widget_type);
    if (editor) {
        for (const field of [...editor.fields, ...(editor.disclosureSettings || [])]) {
            const style = /style/.test(field.key);
            const label = field.kind === 'label' || /_(?:label|current_label)(?:_\d+)?$/.test(field.key) ||
                /^widget_list_label_(?:top|bottom)$/.test(field.key);
            if (field.binding || field.kind === 'color' || style || label || field.key === 'widget_data_binding') {
                fields.push({ ...field, kind: style ? 'style' : field.kind, label });
            }
        }
    }
    if (button.widget_type === 'sparkline') {
        for (const suffix of ['', '_2', '_3']) {
            fields.push({ key: 'widget_sparkline_ha_entity' + suffix, kind: 'entity', maxBytes: 95 });
        }
    }
    return Array.from(new Map(fields.map(field => [field.key, field])).values()).map(field => {
        const input = field.id && document.getElementById(field.id);
        return { ...field, maxBytes: field.maxBytes ||
            (input && input.maxLength > 0 ? input.maxLength : field.kind === 'style' ? 127 : 191) };
    });
}

function padPasteChanges(button) {
    const groups = new Map();
    function add(kind, original, field, text, start, end) {
        const key = kind + ':' + original;
        if (!groups.has(key)) groups.set(key, { kind, original, value: original, uses: [] });
        groups.get(key).uses.push({ field, text, start, end });
    }
    function bindings(text, field, offset = 0) {
        for (const token of bindingTokenize(text)) {
            if (token.unclosed) continue;
            const bodyStart = token.start + token.scheme.length + 2;
            const nested = bindingTokenize(token.params);
            if (nested.length) {
                bindings(token.params, field, offset + bodyStart);
                continue;
            }
            const source = bindingSplitParams(bindingSplitFallback(token.params).params)[0];
            if (token.scheme.toLowerCase() === 'ha' && /^[a-z0-9_]+\.[a-z0-9_]+$/.test(source.trim())) {
                const start = offset + bodyStart + source.indexOf(source.trim());
                add('entity', source.trim(), field, field.text, start, start + source.trim().length);
            } else {
                add('binding', token.raw, field, field.text, offset + token.start, offset + token.end);
            }
        }
    }
    for (const field of padPasteFields(button)) {
        if (button.widget_type === 'sparkline' && field.kind === 'color') {
            const series = /^widget_sparkline_line_color(_[23])?$/.exec(field.key);
            if (series && !button['widget_data_binding' + (series[1] || '')]?.trim()) continue;
            const reference = /^widget_sparkline_ref_([123])_color$/.exec(field.key);
            if (reference && button['widget_sparkline_ref_' + reference[1] + '_y'] == null) continue;
        }
        const raw = button[field.key];
        if (raw === undefined || raw === null || raw === '') continue;
        const text = field.kind === 'color' ? padColorValue(raw) : String(raw);
        field.text = text;
        if (field.kind === 'entity') {
            add('entity', text, field, text, 0, text.length);
            continue;
        }
        const tokens = bindingTokenize(text);
        bindings(text, field);
        if (field.kind === 'color') {
            const color = padColorNormalizeLiteral(text);
            if (color) add('color', color, field, text, 0, text.length);
        } else if (field.kind === 'style') {
            let offset = 0;
            for (const part of bindingSplitParams(text)) {
                const match = /^(\s*color\s*:\s*)(.*?)\s*$/.exec(part);
                const color = match && padColorNormalizeLiteral(match[2]);
                if (color) add('color', color, field, text, offset + match[1].length,
                    offset + match[1].length + match[2].length);
                offset += part.length + 1;
            }
        } else if (field.label && !tokens.length) {
            add('label', text, field, text, 0, text.length);
        }
    }
    return Array.from(groups.values());
}

function padPasteApplyChanges(button, rows) {
    const copy = padCloneJson(button);
    const edits = new Map();
    for (const row of rows) {
        if (row.value === row.original) continue;
        let value = row.kind === 'label' ? row.value : row.value.trim();
        if (row.kind === 'entity' && !/^[a-z0-9_]+\.[a-z0-9_]+$/.test(value)) {
            throw new Error('Enter a Home Assistant entity ID, such as sensor.room_temperature.');
        }
        if (row.kind === 'binding') {
            const tokens = bindingTokenize(value);
            if (tokens.length !== 1 || tokens[0].raw !== value || tokens[0].unclosed) {
                throw new Error('Enter one complete binding, including its brackets.');
            }
        }
        if (row.kind === 'color') {
            value = padColorNormalizeLiteral(value);
            if (!value) throw new Error('Enter a literal color, such as #2196F3.');
        }
        for (const use of row.uses) {
            if (!edits.has(use.field.key)) edits.set(use.field.key, []);
            edits.get(use.field.key).push({ ...use, value });
        }
    }
    for (const [key, replacements] of edits) {
        const field = replacements[0].field;
        let text = replacements[0].text;
        for (const replacement of replacements.sort((a, b) => b.start - a.start)) {
            text = text.slice(0, replacement.start) + replacement.value + text.slice(replacement.end);
        }
        if (new TextEncoder().encode(text).length > field.maxBytes) {
            throw new Error(key + ' exceeds its ' + field.maxBytes + '-byte limit.');
        }
        const validation = validateBinding(text, field.binding || {});
        if (!validation.valid) throw new Error(key + ': ' + validation.errors[0].message);
        copy[key] = text;
    }
    return copy;
}

function padPasteDialogOpen() {
    const dialog = document.getElementById('pad-paste-dialog');
    const list = document.getElementById('pad-paste-fields');
    const error = document.getElementById('pad-paste-error');
    const submit = document.getElementById('pad-paste-confirm');
    const snapshot = padCloneJson(padState.btnClipboard);
    const rows = padPasteChanges(snapshot);
    const destination = [padState.page, padState.selectedLayer, padState.editCol, padState.editRow];
    list.replaceChildren();
    error.textContent = '';
    submit.textContent = 'Paste';
    const update = () => {
        submit.textContent = rows.some(row => row.value !== row.original) ? 'Paste with changes' : 'Paste';
        error.textContent = '';
    };
    rows.forEach((row, index) => {
        const group = document.createElement('div');
        group.className = 'form-group';
        const label = document.createElement('label');
        const input = document.createElement('input');
        input.type = 'text';
        input.id = 'pad-paste-value-' + index;
        input.value = row.kind === 'label' ? padLabelToInput(row.value) : row.value;
        input.spellcheck = false;
        label.htmlFor = input.id;
        const names = { entity: 'Home Assistant', color: 'Color', binding: 'Binding' };
        const labelNames = { label_top: 'Top label', label_center: 'Center label', label_bottom: 'Bottom label' };
        const name = row.kind === 'label' && row.uses.length > 1 ? 'Label' :
            (names[row.kind] || labelNames[row.uses[0].field.key] ||
            row.uses[0].field.key.replaceAll('_', ' '));
        label.textContent = name +
            ' - ' + row.uses.length + (row.uses.length === 1 ? ' use' : ' uses');
        label.title = row.uses.map(use => use.field.key).join(', ');
        group.appendChild(label);
        input.addEventListener('input', () => {
            row.value = row.kind === 'label' ? input.value.replace(/\\n/g, '\n') : input.value;
            update();
        });
        if (row.kind === 'color') {
            const container = document.createElement('div');
            container.className = 'bindable-color';
            const swatch = document.createElement('button');
            swatch.type = 'button';
            swatch.className = 'bc-swatch';
            swatch.setAttribute('aria-label', 'Choose replacement color');
            input.className = 'bc-input';
            input.dataset.allowBindings = 'false';
            container.append(swatch, input);
            group.appendChild(container);
            padInitBindableColor(container);
        } else {
            input.className = 'form-control';
            group.appendChild(input);
        }
        list.appendChild(group);
    });
    document.getElementById('pad-paste-empty').hidden = rows.length > 0;
    document.getElementById('pad-paste-cancel').onclick = () => dialog.close();
    dialog.onclose = () => {
        if (_cpPopover && dialog.contains(_cpPopover.pop)) padColorPopoverClose();
        list.replaceChildren();
        submit.onclick = null;
    };
    submit.onclick = () => {
        try {
            if (destination.some((value, index) =>
                value !== [padState.page, padState.selectedLayer, padState.editCol, padState.editRow][index])) {
                throw new Error('The destination changed. Cancel and paste again.');
            }
            const button = padPasteApplyChanges(snapshot, rows);
            if (!padDialogPasteButton(button)) return;
            dialog.close();
        } catch (failure) {
            error.textContent = failure.message;
        }
    };
    dialog.showModal();
}
