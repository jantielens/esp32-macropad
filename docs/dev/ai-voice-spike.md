# AI Voice Spike Archive

## Status

This document records a validated, non-production spike developed in the
`release/1.25.0` worktree. It is an archive for the planned AI Voice work, not
the long-term architecture or public feature contract.

The last validation build was `./build.sh esp32-p4-lcd4b` on 2026-08-08. The
spike is preserved on the `spike/azure-stt-p4` branch.

## Proven Flow

The `esp32-p4-lcd4b` board can complete this workflow:

1. A button action starts microphone capture.
2. A second action stops capture and sends an in-memory WAV to Azure AI
   Foundry's OpenAI-compatible transcription endpoint.
3. The firmware stores the completed transcript in `[stt:text]`.
4. The Stop and transcribe action can optionally publish a successful
   transcript as a non-retained MQTT message.
5. Existing MQTT bindings can render an LLM response received on a configured
   topic in a pad button.

## Hardware Findings

The board uses two codecs on a shared 48 kHz I2S transport:

* ES8311 provides speaker output.
* ES7210 provides microphone ADC input through `SDOUT2` on GPIO 11.

The STT spike captures one microphone channel, decimates 48 kHz stereo frames
to 16 kHz mono, and creates a 16-bit PCM WAV in PSRAM. The recording is capped
at 30 seconds and is discarded after upload.

## Validated Results

* Nonzero microphone samples were captured after ES7210 initialization.
* Azure returned HTTP 200 with a completed transcript.
* `[stt:status]` and `[stt:text]` resolve for display bindings.
* The optional MQTT topic publishes only after successful transcription.
* `node tests/test_portal_action_picker.js` and the P4 firmware build passed.

## Intentional Limitations

* Azure-specific STT is embedded in the main firmware rather than an AI Voice
  device class.
* Audio input is not a reusable generic PCM capture HAL.
* The `stt_mqtt_topic` action field is a spike shortcut, not the planned
  asynchronous action continuation model.
* MQTT messages use a plain transcript payload with no correlation ID.
* TLS certificate validation is not configured for the Azure client.
* TTS, wake word detection, retries, offline transcription, and persistent
  conversation state are out of scope.

## Planned Migration

1. Add generic, board-gated audio input and the ES7210 microphone driver.
2. Add bounded, token-based asynchronous action continuations.
3. Create the `voice_assistant` device class and migrate cloud STT into it.
4. Add optional correlated MQTT envelopes, TTS, and wake word support in later
   work.

## Credential Handling

The local `src/app/stt_credentials.h` file must remain ignored by Git. It
contains provider credentials and must never be committed or included in logs.