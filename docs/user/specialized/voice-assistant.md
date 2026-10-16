---
title: Configure Voice Assistant
description: Configure separate transcription and speech credentials, chain recording to automation, and avoid response loops.
---

Voice Assistant turns recorded speech into text for an automation and can
speak text responses through the device. Recording, cloud transcription,
external response handling, and speech playback are separate steps rather
than a self-contained offline assistant.

This workflow needs firmware with microphone/voice support and a working
network. [Audio](../media/audio.md) owns gain, volume, and playback priority;
[board notes](../hardware.md) own microphone/output wiring.
Audio/text sent to a cloud service can contain private information.
Use an intentional recording control and protect API keys.

## Configure speech services

Open **Voice Assistant** and configure the services you use:

* Speech-to-Text: Azure AI Foundry host, transcription deployment, API key,
  optional two-letter language such as `en`. Empty language enables auto-detection.
* Text-to-Speech: separate host, speech deployment, API key, default voice,
  optional language and instructions.

Enter the resource hostname, not a pasted full request path.
Save each section independently. Blank key input preserves the saved key and
the portal does not return it.
TTS defaults to voice alloy; a voice name is not a language code.
Language becomes a speech instruction, and custom instructions are passed
verbatim. Use deployment-supported settings.

## Record, transcribe, and continue

In a pad's tap actions, add **Record voice** with Record until silence.
It waits for speech, then requires a quiet interval, default 1000 ms with
silence threshold 2/100 of the microphone RMS scale.
Configure input gain first; otherwise quiet rooms may never count as quiet.

Add **Publish MQTT message** next, with your request topic and:

```text
[stt:text]
```

Save Pad and test a short non-sensitive phrase.
The list pauses until transcription succeeds. Stop and transcribe ends recording
early; Cancel discards it.
An error or cloud timeout stops the remaining list rather than publishing stale
text. There is no automatic retry of the timed-out transcription.

Use `[stt:status]` and `[stt:text]` for feedback.
States include idle, recording, listening, transcribing, ready, and error;
text is the latest transcript or error. Do not assume a previous ready value
belongs to a new recording.

## Speak a response

Add **Speak text** with a literal or binding, optional voice, and volume override.
For an automation reply, subscribe to a distinct MQTT response topic through a
[trigger](../device/triggers.md) and speak its message binding.
Keep request and reply topics separate to prevent feedback loops.

TTS download/playback is best-effort and does not pause later actions until
speech finishes. A new request supersedes earlier speech and stale downloads.
TTS errors appear in logs, not the STT status/text bindings.
Your external automation owns response ordering/correlation; delayed older
replies are not automatically matched to a recording.

[Specialized workflows](README.md) | [User guides](../README.md)
