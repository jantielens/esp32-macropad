window.init_timezone_fragment = async function() {
    var form = document.getElementById('timezone-form');
    if (!form) return;
    var selector = document.getElementById('timezone-city');
    var input = document.getElementById('timezone');
    var save = document.getElementById('timezone-save');
    var previewRevision = 0;
    var mounted = function() { return document.getElementById('timezone-form') === form; };
    async function preview() {
        var revision = ++previewRevision;
        document.getElementById('timezone-device-time').textContent = 'Loading';
        document.getElementById('timezone-sync').textContent = '';
        try {
            var response = await fetch('/api/component/timezone/preview?timezone=' + encodeURIComponent(input.value.trim()));
            var data = await response.json();
            if (!mounted() || revision !== previewRevision) return;
            if (!response.ok) throw new Error(data.message || 'Device time unavailable');
            var offset = data.utc_offset || '';
            document.getElementById('timezone-device-time').textContent = data.epoch >= 1704067200
                ? data.local_time + ' \u00b7 UTC' + offset.slice(0, 3) + ':' + offset.slice(3) : 'Unavailable';
            document.getElementById('timezone-sync').textContent = data.ready ? 'Synchronized' : 'Waiting for time synchronization';
        } catch (error) {
            if (!mounted() || revision !== previewRevision) return;
            document.getElementById('timezone-device-time').textContent = 'Unavailable';
            document.getElementById('timezone-sync').textContent = error.message;
        }
    }
    try {
        var responses = await Promise.all([fetch('/api/component/timezone/catalog'), fetch('/api/config')]);
        if (!responses[0].ok || !responses[1].ok) throw new Error('Timezone configuration unavailable');
        var cities = (await responses[0].json()).cities;
        var config = await responses[1].json();
        if (!mounted()) return;
        var groups = {};
        cities.forEach(function(city) {
            var parts = city.name.split('/');
            var groupName = parts.length > 1 ? parts[0] : 'UTC';
            if (!groups[groupName]) {
                groups[groupName] = document.createElement('optgroup');
                groups[groupName].label = groupName;
                selector.appendChild(groups[groupName]);
            }
            groups[groupName].appendChild(new Option(parts[parts.length - 1].replace(/_/g, ' '), city.name));
        });
        selector.appendChild(new Option('Custom', 'custom'));
        input.value = config.timezone || 'UTC0';
        var selected = cities.find(function(city) { return city.name === input.value; })
            || cities.find(function(city) { return city.posix === input.value; });
        selector.value = selected ? selected.name : 'custom';
        document.getElementById('timezone-custom').hidden = selector.value !== 'custom';
        selector.disabled = false;
        save.disabled = false;
        if (typeof dropdownRefresh === 'function') dropdownRefresh(selector);
        selector.addEventListener('change', function() {
            var city = cities.find(function(item) { return item.name === selector.value; });
            if (city) input.value = city.posix;
            document.getElementById('timezone-custom').hidden = !!city;
            preview();
        });
        input.addEventListener('change', preview);
        form.addEventListener('submit', async function(event) {
            event.preventDefault();
            save.disabled = true;
            try {
                var response = await fetch('/api/config?no_reboot=1', {
                    method: 'POST', headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({timezone: input.value.trim()})
                });
                var result = await response.json();
                if (!response.ok || !result.success) throw new Error(result.message || 'Timezone was not saved');
                if (!mounted()) return;
                showMessage('Timezone saved', 'success');
                await preview();
            } catch (error) { if (mounted()) showMessage(error.message, 'error'); }
            finally { if (mounted()) save.disabled = false; }
        });
        await preview();
    } catch (error) { if (mounted()) showMessage(error.message, 'error'); }
};