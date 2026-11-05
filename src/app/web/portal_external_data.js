window.init_external_data_fragment = function () {
    const editor = document.getElementById('external-data-sources');
    const status = document.getElementById('external-data-status');
    const message = document.getElementById('external-data-message');
    const save = document.getElementById('external-data-save');

    function render(sources) {
        status.replaceChildren();
        if (!sources.length) {
            status.textContent = 'No sources configured.';
            return;
        }
        sources.forEach(function (source) {
            const line = document.createElement('p');
            let text = source.name + ': ' + source.state;
            if (source.age_seconds !== undefined) text += ' (last success ' + source.age_seconds + ' seconds ago)';
            if (source.error) text += ' — ' + source.error;
            line.textContent = text;
            status.appendChild(line);
        });
    }

    async function refresh(updateEditor) {
        const response = await fetch('/api/config', { cache: 'no-store' });
        if (!response.ok) throw new Error('Unable to load source configuration.');
        const config = await response.json();
        if (!Array.isArray(config.external_data)) throw new Error('External data is unavailable.');
        render(config.external_data);
        if (updateEditor) {
            editor.value = JSON.stringify(config.external_data.map(function (source) {
                return { name: source.name, url: source.url, refresh_seconds: source.refresh_seconds };
            }), null, 2);
        }
    }

    async function poll() {
        if (!status.isConnected) return;
        try {
            await refresh(false);
        } catch (error) {
            status.textContent = error.message;
        }
        if (status.isConnected) setTimeout(poll, 10000);
    }

    save.addEventListener('click', async function () {
        let sources;
        try {
            sources = JSON.parse(editor.value);
        } catch (error) {
            message.textContent = 'Enter a valid JSON array.';
            return;
        }
        if (!Array.isArray(sources)) {
            message.textContent = 'Enter a JSON array.';
            return;
        }
        save.disabled = true;
        try {
            const response = await fetch('/api/config?no_reboot=1', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ external_data: sources })
            });
            if (!response.ok) throw new Error('Save failed. Check source names, URLs, intervals, and device availability.');
            await refresh(false).catch(function (error) {
                status.textContent = error.message;
            });
            message.textContent = '';
            showMessage('Sources saved.', 'success');
        } catch (error) {
            message.textContent = error.message;
        } finally {
            save.disabled = false;
        }
    });

    refresh(true).then(function () {
        setTimeout(poll, 10000);
    }).catch(function (error) {
        message.textContent = error.message;
    });
};
