(function () {
  'use strict';

  var current = null;
  // The shell loads this script before the portal installs its fetch limiter.
  var streamFetch = window.fetch.bind(window);

  function updateStatus(text, active) {
    var status = document.getElementById('live-microphone-status');
    if (status) status.textContent = text;
    if (window.microphoneStream.onStatus) window.microphoneStream.onStatus(text, active);
  }

  function stopListening(message) {
    var session = current;
    current = null;
    if (session) {
      session.controller.abort();
      session.sources.forEach(function (source) {
        try { source.stop(); } catch (error) { console.warn('Audio stop failed:', error); }
      });
      session.context.close().catch(function (error) {
        console.warn('Audio context close failed:', error);
      });
    }
    updateStatus(message || 'Not listening', false);
  }

  function playPcm(session, bytes) {
    var frameBytes = session.channels * 2;
    var length = bytes.length - bytes.length % frameBytes;
    if (!length) return bytes;
    if (session.nextTime > session.context.currentTime + 0.7) {
      session.sources.forEach(function (source) { source.stop(); });
      session.sources.clear();
      session.nextTime = session.context.currentTime;
    }
    var frameCount = length / frameBytes;
    var audio = session.context.createBuffer(session.channels, frameCount, session.sampleRate);
    var samples = new DataView(bytes.buffer, bytes.byteOffset, length);
    for (var channel = 0; channel < session.channels; ++channel) {
      var output = audio.getChannelData(channel);
      for (var frame = 0; frame < frameCount; ++frame) {
        output[frame] = samples.getInt16(frame * frameBytes + channel * 2, true) / 32768;
      }
    }
    var source = session.context.createBufferSource();
    source.buffer = audio;
    source.connect(session.context.destination);
    session.sources.add(source);
    source.onended = function () { session.sources.delete(source); };
    session.nextTime = Math.max(session.nextTime, session.context.currentTime + 0.05);
    source.start(session.nextTime);
    session.nextTime += audio.duration;
    return bytes.slice(length);
  }

  async function startListening() {
    if (current) return;
    var AudioContextClass = window.AudioContext || window.webkitAudioContext;
    if (!AudioContextClass) {
      updateStatus('Browser audio is unavailable', false);
      return;
    }
    var context;
    try {
      context = new AudioContextClass();
    } catch (error) {
      updateStatus('Browser audio is unavailable: ' + error.message, false);
      return;
    }
    var session = {
      context: context,
      controller: new AbortController(),
      sources: new Set(),
      nextTime: 0,
      sampleRate: 0,
      channels: 0
    };
    current = session;
    updateStatus('Starting...', true);
    try {
      await session.context.resume();
      var response = await streamFetch('/api/component/microphone/stream', {
        headers: { 'X-Microphone-Stream': '1' },
        signal: session.controller.signal
      });
      if (!response.ok) {
        var error = await response.json();
        throw new Error(error.error || 'Unable to start listening');
      }
      var reader = response.body.getReader();
      var pending = new Uint8Array(0);
      while (current === session) {
        var result = await reader.read();
        if (current !== session) break;
        if (result.done) throw new Error('Microphone stream ended');
        var combined = new Uint8Array(pending.length + result.value.length);
        combined.set(pending);
        combined.set(result.value, pending.length);
        pending = combined;
        if (!session.sampleRate && pending.length >= 12) {
          var header = new DataView(pending.buffer, pending.byteOffset, 12);
          if (header.getUint8(0) !== 76 || header.getUint8(1) !== 77 || header.getUint8(2) !== 1) {
            throw new Error('Invalid microphone stream');
          }
          if (header.getUint8(3) !== 1) {
            throw new Error(header.getUint8(3) === 2 ? 'Microphone is busy or unavailable' : 'Microphone unavailable');
          }
          session.sampleRate = header.getUint32(4, true);
          session.channels = header.getUint8(8);
          if (!session.sampleRate || (session.channels !== 1 && session.channels !== 2) ||
              header.getUint8(9) !== 16) {
            throw new Error('Unsupported microphone format');
          }
          pending = pending.slice(12);
          updateStatus('Listening', true);
        }
        if (session.sampleRate) pending = playPcm(session, pending);
      }
    } catch (error) {
      if (current === session) stopListening(error.message || 'Microphone stream failed');
    }
  }

  window.microphoneStream = { start: startListening, stop: stopListening };
}());
