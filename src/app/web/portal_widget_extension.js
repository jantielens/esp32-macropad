(() => {
    let mount;
    function populate(selected) {
        const select = document.getElementById('pad-edit-extension-id');
        if (!mount || mount.select !== select) return;
        select.replaceChildren();
        const placeholder = document.createElement('option');
        placeholder.value = mount.status === 'loading' ? selected : '';
        placeholder.textContent = mount.status === 'loading' ? 'Loading extensions...' :
            mount.status === 'failed' ? 'Extension catalog unavailable' :
                mount.slots.length ? '(none)' : 'No enabled extensions installed';
        select.appendChild(placeholder);
        mount.slots.forEach(slot => {
            const option = document.createElement('option');
            option.value = slot.id;
            option.textContent = slot.id + ' @ ' + slot.version;
            select.appendChild(option);
        });
        select.value = mount.status === 'loading' || mount.slots.some(slot => slot.id === selected) ? selected : '';
        select.disabled = mount.status !== 'ready';
    }
    padRegisterWidgetEditor({
        type: 'external',
        available: info => !!(info && info.has_native_extensions === true),
        replacesContent: true,
        sections: [{ id: 'pad-edit-extension-section', tab: 'Content' }],
        actions: { tap: true, longPress: true },
        fields: [
            { id: 'pad-edit-extension-config', key: 'extension_config', kind: 'text', default: '' },
            { id: 'pad-edit-extension-tick-interval', key: 'extension_tick_interval_ms', kind: 'number', default: '' },
            { id: 'pad-edit-extension-upscale', key: 'extension_upscale', kind: 'number', default: 1 }
        ],
        init(onSettled = () => {}) {
            const current = { select: document.getElementById('pad-edit-extension-id'), status: 'loading', slots: [] };
            mount = current;
            populate('');
            if (!padWidgetEditor('external').supported) return;
            return fetch('/api/extensions').then(response => {
                if (!response.ok) throw new Error('Extension catalog unavailable');
                return response.json();
            }).then(catalog => {
                if (mount !== current || document.getElementById('pad-edit-extension-id') !== current.select) return;
                const selected = current.select.value;
                current.slots = (Array.isArray(catalog.slots) ? catalog.slots : []).filter(slot => slot.installed && slot.enabled);
                current.status = 'ready';
                populate(selected);
                onSettled();
            }).catch(() => {
                if (mount !== current || document.getElementById('pad-edit-extension-id') !== current.select) return;
                current.status = 'failed';
                populate('');
                onSettled();
            });
        },
        load(button) { populate(button.extension_id || ''); },
        build(button) {
            const fieldIds = ['pad-edit-extension-id'];
            if (!mount || mount.status !== 'ready') throw Object.assign(new Error(mount && mount.status === 'failed' ?
                'Extension catalog unavailable; reload the editor to retry' : 'Extension catalog is still loading'), { fieldIds });
            const selected = mount.select.value;
            if (!mount.slots.some(slot => slot.id === selected)) throw Object.assign(new Error('Select an installed enabled extension'), { fieldIds });
            button.extension_id = selected;
            if (!Number.isInteger(button.extension_upscale) || button.extension_upscale < 1 || button.extension_upscale > 4) {
                throw Object.assign(new Error('Select an upscale from 1 to 4'), { fieldIds: ['pad-edit-extension-upscale'] });
            }
        },
        dispose() { mount = undefined; }
    });
})();