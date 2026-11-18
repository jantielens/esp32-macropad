(function () {
  'use strict';
  var active = false;
  var timer = null;
  var controller = null;
  var savedGain = '0';
  var savedRumble = false;
  var heldPeak = 0;
  var heldUntil = 0;
  var inputClipUntil = 0;
  var gainClipUntil = 0;
  var listening = false;

  registerConfigFields(['microphone_gain_db', 'microphone_reduce_rumble']);

  function element(id) { return document.getElementById(id); }
  function updateForm() {
    var gain = element('microphone_gain_db');
    var rumble = element('microphone_reduce_rumble');
    if (!gain || !rumble) return;
    var value = Number(gain.value);
    element('microphone-gain-value').textContent = (value > 0 ? '+' : '') + value + ' dB';
    element('microphone-unsaved').hidden = gain.value === savedGain && rumble.checked === savedRumble;
  }
  function stopTest() {
    active = false;
    clearTimeout(timer);
    timer = null;
    if (controller) controller.abort();
    controller = null;
    if (window.microphoneStream) window.microphoneStream.stop();
    var toggle = element('microphone-test');
    if (toggle) {
      toggle.checked = false;
      element('microphone-test-status').textContent = 'Test stopped';
      element('microphone-peak').textContent = 'Peak: unavailable';
      element('microphone-warning').textContent = '';
      element('microphone-level').hidden = true;
      var hear = element('microphone-hear');
      if (hear) hear.disabled = true;
    }
    heldPeak = 0;
    heldUntil = 0;
    inputClipUntil = 0;
    gainClipUntil = 0;
  }
  async function poll() {
    if (!active) return;
    controller = new AbortController();
    var request = controller;
    try {
      var response = await fetch('/api/component/microphone/levels', {
        headers: { 'X-Microphone-Test': '1' }, cache: 'no-store', signal: request.signal
      });
      if (!response.ok) throw new Error('Unable to read microphone levels');
      var data = await response.json();
      if (!active || controller !== request) return;
      var status = !data.available ? 'Microphone unavailable' :
        data.ota ? 'Firmware update in progress' :
        data.in_use && !listening ? 'Microphone in use; showing current input when available' :
        data.fresh ? 'Testing current settings' : data.sampled ? 'Microphone level stale' : 'Starting';
      element('microphone-test-status').textContent = status;
      var now = Date.now();
      if (data.input_full_scale) inputClipUntil = now + 2000;
      if (data.gain_clipped) gainClipUntil = now + 2000;
      var warnings = [];
      if (now < inputClipUntil) warnings.push('Input reached full scale');
      if (now < gainClipUntil) warnings.push('Digital gain clipped');
      element('microphone-warning').textContent = warnings.join('. ');
      if (data.fresh) {
        element('microphone-level').hidden = false;
        var peakDb = data.peak > 0 ? Math.min(0, 20 * Math.log10(data.peak / 32768)) : -Infinity;
        var meterDb = Math.max(-60, peakDb);
        var level = (meterDb + 60) * 100 / 60;
        element('microphone-level').setAttribute('aria-valuenow', meterDb.toFixed(1));
        element('microphone-level').setAttribute('aria-valuetext',
          peakDb === -Infinity ? 'Silence' : peakDb.toFixed(1) + ' dBFS');
        element('microphone-level-bar').style.width = level + '%';
        element('microphone-level-bar').className = 'progress-bar ' +
          (peakDb >= -1 ? 'bg-danger' : 'bg-success');
        if (data.peak >= heldPeak || now >= heldUntil) {
          heldPeak = data.peak;
          heldUntil = now + 2000;
        }
        element('microphone-peak').textContent = 'Peak: ' +
          (heldPeak ? (20 * Math.log10(heldPeak / 32768)).toFixed(1) + ' dBFS' : '-inf dBFS');
      } else {
        element('microphone-level').hidden = true;
        element('microphone-peak').textContent = 'Peak: unavailable';
      }
    } catch (error) {
      if (active && controller === request && error.name !== 'AbortError') {
        element('microphone-test-status').textContent = error.message || 'Microphone test unavailable';
        element('microphone-peak').textContent = 'Peak: unavailable';
        element('microphone-level').hidden = true;
      }
    }
    if (active && controller === request) {
      controller = null;
      timer = setTimeout(poll, 200);
    }
  }
  function startTest() {
    if (active) return;
    active = true;
    element('microphone-test').checked = true;
    element('microphone-test-status').textContent = 'Starting';
    var hear = element('microphone-hear');
    if (hear) hear.disabled = false;
    poll();
  }
  window.init_microphone_fragment = async function () {
    var gain = element('microphone_gain_db');
    var rumble = element('microphone_reduce_rumble');
    var save = element('microphone-save');
    var undo = element('microphone-undo');
    var defaults = element('microphone-defaults');
    var test = element('microphone-test');
    gain.disabled = rumble.disabled = save.disabled = undo.disabled = defaults.disabled = test.disabled = true;
    var config = await loadConfig();
    if (element('microphone_gain_db') !== gain) return;
    if (!config || config.microphone_gain_db === undefined || config.microphone_reduce_rumble === undefined) {
      element('microphone-test-status').textContent = 'Microphone settings unavailable';
      element('microphone-save').disabled = true;
      return;
    }
    savedGain = String(config.microphone_gain_db);
    savedRumble = config.microphone_reduce_rumble;
    var appliedGain = String(config.current_microphone_gain_db);
    var appliedRumble = config.current_microphone_reduce_rumble;
    if (config.current_microphone_gain_db === undefined || appliedRumble === undefined) {
      element('microphone-test-status').textContent = 'Current microphone settings unavailable';
      return;
    }
    gain.value = appliedGain;
    rumble.checked = appliedRumble;
    var pendingApply = Promise.resolve();
    var busy = false;
    function refresh() {
      updateForm();
      var dirty = appliedGain !== savedGain || appliedRumble !== savedRumble;
      gain.disabled = rumble.disabled = defaults.disabled = busy;
      save.disabled = undo.disabled = busy || !dirty;
    }
    async function applySettings(factory) {
      var requestedGain = gain.value;
      var requestedRumble = rumble.checked;
      var factoryApplied = false;
      busy = true;
      refresh();
      element('microphone-unsaved').hidden = false;
      element('microphone-unsaved').textContent = 'Applying...';
      try {
        var response = await fetch('/api/component/microphone/settings', {
          method: 'PUT',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({microphone_gain_db: Number(requestedGain),
            microphone_reduce_rumble: requestedRumble})
        });
        var result = await response.json();
        if (!response.ok || !result.success) throw new Error(result.message || 'Unable to apply microphone settings');
        appliedGain = requestedGain;
        appliedRumble = requestedRumble;
        factoryApplied = factory;
      } catch (error) {
        if (element('microphone_gain_db') === gain)
          showMessage(error.message || 'Unable to apply microphone settings', 'error');
      }
      if (element('microphone_gain_db') !== gain) return;
      gain.value = appliedGain;
      rumble.checked = appliedRumble;
      busy = false;
      element('microphone-unsaved').textContent = factoryApplied ?
        'Factory values applied. Select Save settings to keep them.' : 'Applied, not saved.';
      refresh();
    }
    function apply(factory) {
      pendingApply = applySettings(factory === true);
      return pendingApply;
    }
    refresh();
    gain.addEventListener('input', updateForm);
    gain.addEventListener('change', apply);
    rumble.addEventListener('change', apply);
    undo.addEventListener('click', function () {
      gain.value = savedGain;
      rumble.checked = savedRumble;
      return apply();
    });
    defaults.addEventListener('click', function () {
      gain.value = '0';
      rumble.checked = false;
      return apply(true);
    });
    save.addEventListener('click', async function () {
      await pendingApply;
      if (element('microphone_gain_db') !== gain) return;
      var submittedGain = appliedGain;
      var submittedRumble = appliedRumble;
      var saving = saveFragmentConfig(false, ['microphone_gain_db', 'microphone_reduce_rumble']);
      busy = true;
      refresh();
      var success = await saving;
      if (element('microphone_gain_db') !== gain) return;
      busy = false;
      if (success) {
        savedGain = submittedGain;
        savedRumble = submittedRumble;
      }
      refresh();
    });
    test.addEventListener('change', function () {
      if (this.checked) startTest();
      else stopTest();
    });
    var hear = element('microphone-hear');
    if (hear && window.microphoneStream) {
      window.microphoneStream.onStatus = function (text, running) {
        listening = running;
        hear.checked = running;
      };
      hear.addEventListener('change', function () {
        if (hear.checked) window.microphoneStream.start();
        else window.microphoneStream.stop();
      });
    }
    test.disabled = false;
  };
  window.addEventListener('portal-fragment-leave', function (event) {
    if (event.detail === 'microphone') stopTest();
  });
  window.addEventListener('pagehide', stopTest);
}());
