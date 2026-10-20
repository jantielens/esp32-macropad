var dialogAppearanceFields = [
    ['font', 'Font', 'font'],
    ['font_size', 'Font size', 'size'],
    ['text_color', 'Text color', 'color'],
    ['horizontal_align', 'Horizontal text alignment', ['Left', 'Center', 'Right']],
    ['vertical_align', 'Vertical text alignment', ['Top', 'Center', 'Bottom']],
    ['background_color', 'Background color', 'color'],
    ['background_opacity', 'Background opacity (%)', 0, 100],
    ['backdrop_opacity', 'Backdrop opacity (%)', 0, 100],
    ['border_color', 'Border color', 'color'],
    ['border_width', 'Border width (px)', 0, 10],
    ['corner_radius', 'Corner radius (px)', 0, 40],
    ['padding', 'Padding (px)', 0, 48],
    ['margin', 'Screen margin (px)', 0, 48],
    ['width', 'Width (%; 0 = Auto)', 0, 100],
    ['height', 'Height (%; 0 = Auto)', 0, 100],
    ['position', 'Position', ['Top', 'Center', 'Bottom']]
];
var dialogAppearanceButtonFields = [
    ['button_width', 'Button width (% each; 0 = Auto)', 0, 50],
    ['button_height', 'Button height (px; 0 = Auto)', 0, 160],
    ['approve_background', 'Approve background', 'color'],
    ['approve_color', 'Approve text color', 'color'],
    ['cancel_background', 'Cancel background', 'color'],
    ['cancel_color', 'Cancel text color', 'color']
];

function dialogAppearanceCollect(panel) {
    var result = {};
    ['notification', 'confirmation'].forEach(function(type) {
        result[type] = {};
        panel.querySelectorAll('[data-dialog-type="' + type + '"]').forEach(function(input) {
            result[type][input.dataset.field] = input.classList.contains('bc-input') || input.dataset.field === 'font' || input.dataset.field === 'background_pattern'
                ? input.value : Number(input.value);
        });
    });
    return result;
}

window.init_dialog_appearance_fragment = async function() {
    var panel = document.getElementById('dialog-appearance-section');
    if (!panel || panel.dataset.initialized) return;
    panel.dataset.initialized = '1';
    var editors = panel.querySelector('#dialog-appearance-editors');
    var status = panel.querySelector('#dialog-appearance-status');
    var save = panel.querySelector('#dialog-appearance-save');
    var form = panel.querySelector('form');
    var info, defaults;
    var loadedFonts = new Map();
    function message(text, type) {
        var content = document.createElement('span');
        content.textContent = text;
        showMessage(content.innerHTML, type);
    }
    async function request(url, options) {
        var response = await fetch(url, options);
        var value = await response.json();
        if (!response.ok || value.success === false) throw new Error(value.message || value.error || 'Request failed (' + response.status + ')');
        return value;
    }
    function input(type, field) { return panel.querySelector('[data-dialog-type="' + type + '"][data-field="' + field + '"]'); }
    function fieldHTML(type, field) {
        var id = 'dialog-' + type + '-' + field[0];
        var attrs = ' id="' + id + '" name="' + id + '" data-dialog-type="' + type + '" data-field="' + field[0] +
            '" class="' + (field[2] === 'color' ? 'bc-input' : 'form-control') + '"';
        var html = '<label for="' + id + '">' + field[1];
        if (field[2] === 'color') {
            return '<div class="dialog-color-field"><span id="' + id + '-label">' + field[1] + '</span>' +
                '<div class="bindable-color"><button type="button" class="bc-swatch" aria-labelledby="' + id + '-label"></button>' +
                '<input type="text"' + attrs + ' data-allow-bindings="false" maxlength="7" required pattern="#[0-9a-fA-F]{6}"></div></div>';
        }
        if (Array.isArray(field[2])) {
            html += '<select' + attrs + '>' + field[2].map(function(label, index) {
                return '<option value="' + index + '">' + label + '</option>';
            }).join('') + '</select>';
        } else if (field[2] === 'size') {
            html += '<span data-font-size-control><select' + attrs + '></select></span>';
        } else if (field[2] === 'font') {
            html += '<select' + attrs + '></select>';
        } else {
            html += '<input' + attrs + ' type="number" min="' + field[2] + '" max="' + field[3] + '" step="1" required>';
        }
        if (field[2] === 'size') {
            html += '<small id="' + id + '-help" class="text-muted" data-font-size-help hidden>8-96 px; leave empty for Automatic.</small>';
        }
        return html + '</label>';
    }
    function groupHTML(type, title, rows) {
        var fields = dialogAppearanceFields.concat(dialogAppearanceButtonFields);
        return '<fieldset class="dialog-control-group"><legend>' + title + '</legend>' +
            rows.map(function(row) {
                return '<div class="dialog-control-row">' + row.map(function(key) {
                    return fieldHTML(type, fields.find(function(field) { return field[0] === key; }));
                }).join('') + '</div>';
            }).join('') + '</fieldset>';
    }
    function rgba(color, opacity) {
        return 'rgba(' + [1, 3, 5].map(function(index) { return parseInt(color.slice(index, index + 2), 16); }).join(',') + ',' + opacity / 100 + ')';
    }
    function fontSizes(type, selected) {
        var font = input(type, 'font').value;
        var custom = (font || defaults.default_font || '@builtin') !== '@builtin';
        var previous = input(type, 'font_size');
        var control = document.createElement(custom ? 'input' : 'select');
        var container = previous.closest('[data-font-size-control]');
        ['id', 'name', 'data-dialog-type', 'data-field'].forEach(function(name) {
            control.setAttribute(name, previous.getAttribute(name));
        });
        control.className = 'form-control';
        if (custom) {
            control.type = 'number';
            control.min = '8';
            control.max = '96';
            control.step = '1';
            control.placeholder = 'Automatic';
            control.setAttribute('aria-describedby', control.id + '-help');
            control.value = Number(selected) ? selected : '';
        } else {
            var sizes = [12,14,18,24,32,36];
            control.add(new Option('Automatic', '0'));
            sizes.forEach(function(size) { control.add(new Option(size + ' px', size)); });
            control.value = sizes.indexOf(Number(selected)) >= 0 ? selected : 0;
        }
        container.parentElement.querySelector('[data-font-size-help]').hidden = !custom;
        container.replaceChildren(control);
    }
    function preview(type) {
        if (!panel.isConnected) return;
        var style = dialogAppearanceCollect(panel)[type];
        var editor = panel.querySelector('[data-dialog-editor="' + type + '"]');
        var screen = editor.querySelector('.dialog-preview-screen');
        var box = editor.querySelector('.dialog-preview-box');
        var text = editor.querySelector('.dialog-preview-message');
        var width = info.display_coord_width || 480;
        var height = info.display_coord_height || 320;
        screen.style.aspectRatio = width + ' / ' + height;
        var scale = screen.clientWidth / width || 1;
        var px = function(value) { return value * scale + 'px'; };
        var fontName = style.font || defaults.default_font || '@builtin';
        var family = loadedFonts.get(fontName);
        if (fontName !== '@builtin' && !family && !loadedFonts.has(fontName) && typeof FontFace !== 'undefined') {
            loadedFonts.set(fontName, null);
            var face = new FontFace('DialogPreview' + loadedFonts.size,
                'url("/api/component/storage/file?path=' + encodeURIComponent('/fonts/' + fontName + '.ttf') + '")');
            face.load().then(function(ready) {
                if (!panel.isConnected) return;
                document.fonts.add(ready);
                loadedFonts.set(fontName, ready.family);
                preview('notification'); preview('confirmation');
            }).catch(function(error) {
                console.warn('Dialog font preview unavailable:', fontName, error);
            });
        }
        var dimensions = {
            width: Math.min(width - 2 * style.margin, width * (style.width || 80) / 100),
            height: Math.max(0, height - 2 * style.margin)
        };
        box.style.width = px(dimensions.width);
        box.style.height = style.height ? px(Math.min(dimensions.height, height * style.height / 100)) : 'auto';
        box.style.maxHeight = px(dimensions.height);
        box.style.left = '50%';
        box.style.top = style.position === 0 ? px(style.margin) : style.position === 1 ? '50%' : 'auto';
        box.style.bottom = style.position === 2 ? px(style.margin) : 'auto';
        box.style.transform = style.position === 1 ? 'translate(-50%, -50%)' : 'translateX(-50%)';
        box.style.padding = px(style.padding);
        var background = editor.querySelector('.dialog-preview-background');
        background.style.backgroundColor = style.background_color;
        background.style.opacity = style.background_opacity / 100;
        var pattern = padFillBackground(style.background_pattern || 'none', style.background_pattern_color,
            style.pattern_spacing * scale, style.pattern_stroke_width * scale);
        background.style.backgroundImage = pattern.image;
        background.style.backgroundSize = pattern.size;
        background.style.borderRadius = px(Math.max(0, style.corner_radius - style.border_width));
        box.style.border = px(style.border_width) + ' solid ' + style.border_color;
        box.style.borderRadius = px(style.corner_radius);
        box.style.color = style.text_color;
        box.style.fontFamily = family ? '"' + family + '", sans-serif' : 'sans-serif';
        box.style.fontSize = px(style.font_size || defaults.base_text_size || 24);
        box.style.lineHeight = '1.2';
        editor.querySelector('.dialog-preview-backdrop').style.background = rgba('#000000', style.backdrop_opacity);
        text.textContent = type === 'confirmation' ? 'Restart the device?\nUnsaved work may be lost.' : 'Scene activated.\nYour room is ready.';
        text.style.textAlign = ['left', 'center', 'right'][style.horizontal_align];
        text.style.alignContent = ['start', 'center', 'end'][style.vertical_align];
        if (type === 'confirmation') {
            var row = editor.querySelector('.dialog-preview-buttons');
            row.style.gap = px(8);
            row.style.marginTop = px(8);
            ['cancel', 'approve'].forEach(function(button) {
                var control = row.querySelector('[data-button="' + button + '"]');
                control.textContent = button === 'cancel' ? 'Cancel' : 'Continue';
                control.style.width = style.button_width ? 'calc((100% - ' + px(8) + ') * ' + style.button_width / 100 + ')' : 'calc((100% - ' + px(8) + ') / 2)';
                control.style.minWidth = px(44);
                control.style.minHeight = px(44);
                control.style.height = style.button_height ? px(style.button_height) : 'auto';
                control.style.padding = px(8);
                control.style.background = style[button + '_background'];
                control.style.color = style[button + '_color'];
                control.style.borderRadius = px(style.corner_radius);
            });
        }
    }
    status.textContent = 'Loading dialog appearance...';
    try {
        var data = await Promise.all([request('/api/config'), getDeviceInfo(), request('/api/component/button-defaults/config')]);
        if (!panel.isConnected) return;
        info = data[1]; defaults = data[2];
        var templates = data[0].dialog_appearance;
        if (!templates || !templates.notification || !templates.confirmation) throw new Error('Dialog appearance missing from device configuration');
        var fonts = info.has_ttf_fonts ? await request('/api/fonts/list') : [];
        if (!panel.isConnected) return;
        ['notification', 'confirmation'].forEach(function(type) {
            var confirmation = type === 'confirmation';
            var editor = editors.querySelector('[data-dialog-editor="' + type + '"]');
            editor.querySelector('.card-body').innerHTML = '<div class="dialog-appearance-layout"><div class="dialog-appearance-controls">' +
                groupHTML(type, 'Text', [['font', 'font_size', 'text_color'], ['horizontal_align', 'vertical_align']]) +
                groupHTML(type, 'Background', [['background_color', 'background_opacity', 'backdrop_opacity']]) +
                groupHTML(type, 'Border & corners', [['border_color', 'border_width', 'corner_radius']]) +
                groupHTML(type, 'Size & placement', [['width', 'height'], ['position', 'margin', 'padding']]) +
                (confirmation ? groupHTML(type, 'Buttons', [['button_width', 'button_height']]) +
                    groupHTML(type, 'Approve button', [['approve_background', 'approve_color']]) +
                    groupHTML(type, 'Cancel button', [['cancel_background', 'cancel_color']]) : '') +
                '</div><div class="dialog-preview-column"><div class="dialog-preview-screen" role="img" aria-label="Approximate ' + type + ' preview">' +
                '<div class="dialog-preview-pad">' + ['Lights','Music','Weather','Scene','Timer','Settings'].map(function(label) { return '<span>' + label + '</span>'; }).join('') + '</div>' +
                '<div class="dialog-preview-backdrop"></div><div class="dialog-preview-box"><div class="dialog-preview-background"></div><div class="dialog-preview-message"></div>' +
                (confirmation ? '<div class="dialog-preview-buttons"><span data-button="cancel"></span><span data-button="approve"></span></div>' : '') +
                '</div></div><p class="dialog-preview-note">Approximate preview; device rendering may differ</p>' +
                '<button type="button" class="btn btn-secondary" data-dialog-test="' + type + '">Test now</button></div></div>';
            var fontSelect = input(type, 'font');
            fontSelect.add(new Option('Device default', ''));
            fontSelect.add(new Option('Montserrat (built-in)', '@builtin'));
            fonts.forEach(function(font) { fontSelect.add(new Option(font.name, font.name)); });
            if (templates[type].font && !Array.from(fontSelect.options).some(function(option) { return option.value === templates[type].font; }))
                fontSelect.add(new Option(templates[type].font + ' (unavailable)', templates[type].font));
            var backgroundInput = input(type, 'background_color');
            var controls = document.createElement('div');
            controls.id = 'dialog-' + type + '-pattern-controls';
            controls.hidden = true;
            var attrs = ' data-dialog-type="' + type + '" ';
            controls.innerHTML = '<select' + attrs + 'data-field="background_pattern">' +
                PAD_FILL_PATTERNS.map(function(entry) { return '<option value="' + entry[0] + '">' + entry[1] + '</option>'; }).join('') + '</select>' +
                '<input type="text" class="bc-input"' + attrs + 'data-field="background_pattern_color" data-allow-bindings="false" maxlength="7">' +
                '<input type="number"' + attrs + 'data-field="pattern_spacing" data-fill-spacing>' +
                '<input type="number"' + attrs + 'data-field="pattern_stroke_width" data-fill-stroke>';
            backgroundInput.dataset.fillControls = controls.id;
            backgroundInput.closest('.dialog-color-field').appendChild(controls);
            Object.keys(templates[type]).forEach(function(key) {
                var control = input(type, key);
                if (control) control.value = templates[type][key];
            });
            fontSizes(type, templates[type].font_size);
            editor.querySelectorAll('.bindable-color').forEach(padInitBindableColor);
            editor.addEventListener('input', function() {
                preview(type);
            });
            editor.addEventListener('change', function(event) {
                if (event.target.dataset.field === 'font') fontSizes(type, input(type, 'font_size').value);
                preview(type);
            });
            editor.querySelector('[data-dialog-test]').addEventListener('click', async function(event) {
                if (!Array.from(editor.querySelectorAll('input, select')).every(function(control) { return control.reportValidity(); })) return;
                var button = event.currentTarget;
                button.disabled = true;
                try {
                    await request('/api/component/dialog-appearance/test', {
                        method: 'POST', headers: {'Content-Type': 'application/json'},
                        body: JSON.stringify({type: type, appearance: dialogAppearanceCollect(panel)[type]})
                    });
                } catch (error) {
                    message('Could not show sample on device: ' + error.message, 'error');
                } finally { button.disabled = false; }
            });
            preview(type);
        });
        var observer = new ResizeObserver(function() {
            if (!panel.isConnected) { observer.disconnect(); return; }
            preview('notification'); preview('confirmation');
        });
        observer.observe(editors);
        save.disabled = false;
        status.textContent = '';
    } catch (error) { status.textContent = 'Could not load dialog appearance: ' + error.message; }
    form.addEventListener('submit', async function(event) {
        event.preventDefault();
        if (!form.reportValidity() || save.disabled) return;
        var templates = dialogAppearanceCollect(panel);
        for (var type of ['notification', 'confirmation']) {
            var value = templates[type];
            if ((value.width && value.width < 20) || (value.height && value.height < 20) ||
                (value.button_width && value.button_width < 10) || (value.button_height && value.button_height < 44)) {
                message('Custom dimensions: dialog 20-100%; buttons 10-50% wide and 44-160 px high. Use 0 for Auto.', 'error');
                return;
            }
        }
        save.disabled = true;
        try {
            await request('/api/config?no_reboot=1', {method: 'POST', headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({dialog_appearance: templates})});
            message('Saved. Appearance applies to the next dialog.', 'success');
        } catch (error) { message('Could not save dialog appearance: ' + error.message, 'error'); }
        finally { save.disabled = false; }
    });
};
