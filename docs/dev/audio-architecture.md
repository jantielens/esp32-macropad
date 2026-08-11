# Audio Architecture

The audio subsystem produces beeps and MP3 playback through a board-selected
output driver. It keeps command handling, decode work, and hardware I/O in
separate layers so output hardware can vary without changing callers.

## Architecture And Data Flow

`audio.cpp` owns the asynchronous audio worker, command queue, volume state,
and output-starvation timing. `sound_player.cpp` owns MP3 decode, source-rate
conversion, and transfer of interleaved PCM frames to the selected output
driver. `AudioOutputDriver` provides the hardware boundary used by both paths.

```mermaid
flowchart TD
    Actions[Beep actions and playback commands] --> Audio["audio.cpp\nworker and command queue"]
    Storage["LittleFS sound files"] --> Player["sound_player.cpp\nMP3 decode and resampling"]
    Audio --> Driver["AudioOutputDriver"]
    Player --> Driver
    Driver --> Hardware["ES8311 or PCM510xA\nI2S output hardware"]
```

`audio_output_drivers.cpp` includes exactly one concrete driver selected by
`AUDIO_OUTPUT_DRIVER`. New output hardware must implement `AudioOutputDriver`
rather than adding hardware-specific behavior to callers.

## Microphone Input

Boards with `HAS_AUDIO_INPUT` expose a board-neutral capture API in
`audio_input.h`. A caller reserves input with `audio_input_start_capture()`,
reads bounded native, interleaved PCM frames, and releases it with
`audio_input_stop_capture()`. Only one FreeRTOS task can own a capture session
at a time. The input layer does not resample, create WAV files, retain audio,
or make network requests.

`esp32-p4-lcd4b` combines the ES8311 output codec with an ES7210 microphone ADC
on the same 48 kHz I2S clock. The ES8311 driver owns both channels and exposes
the existing RX channel through `AudioInputDriver`; input capture neither
reconfigures the shared I2S transport nor interrupts playback.

Pads can display these read-only microphone bindings:

* `[audio:input.rms]` returns the root-mean-square sound level from 0 to 100.
* `[audio:input.peak]` returns the peak sound level from 0 to 100.
* `[audio:input.active]` returns `true` while the meter is sampling and `false`
    after its short resolver-driven sampling lease expires.

The meter task remains asleep and does not read I2S data unless one of these
bindings is actively resolved.

## Voice Assistant

The `esp32-p4-lcd4b-voice` board variant enables the `voice_assistant` device
class. It reserves the same board-neutral microphone input from a background
worker, downsamples the 48 kHz input to 16 kHz mono PCM WAV in PSRAM, and sends
the recording to Azure AI Foundry for transcription. Recordings are discarded
after the request completes and are never persisted.

The worker is the sole capture owner while recording. `record_start` begins a
manual recording; `record_stop_transcribe` stops it and pauses any later
actions in that action array. `record_until_silence` begins an automatic
recording, detects speech from the same 0-100 RMS scale as `[audio:input.rms]`,
then stops after its configured trailing silence. Its action fields are
`silence_ms` (100-10000, default 1000) and `speech_threshold` (0-100, default
2). It waits for speech before counting silence and has the same 30-second cap
as manual recording. A successful transcription resumes later actions, so a
following ordinary MQTT action can publish `[stt:text]`. During automatic
recording, a `record_stop_transcribe` action requests an immediate stop but the
original automatic action owns the resumed suffix. `record_cancel` discards an
active manual or automatic recording without transcribing or running later
actions. Failed and timed-out work discards the remaining actions.

The Azure API key, host, deployment name, and optional ISO 639-1 language code
are stored in NVS. An empty language code uses Azure auto-detection. The portal
accepts the API key as a write-only password field and only reports whether it
is configured; it never returns or logs the value. The endpoint CA certificate
is versioned in `voice_assistant/azure_ca.h`. MQTT publication is an ordinary
action that uses `[stt:text]` after the transcription continuation succeeds.

Pads can display `[stt:status]` (`idle`, `recording`, `listening`,
`transcribing`, `ready`, or `error`) and `[stt:text]` (the latest transcript or
error message).

The same device class can queue Azure Text-to-Speech MP3 responses. Its worker
downloads bounded MP3 data to PSRAM and transfers that buffer to the audio task;
it never writes a temporary sound file. The audio task owns the buffer after
queueing and frees it on successful completion, decoder errors, a stop request,
or queue flush. TTS action text is the one bindable action value; voice and
volume remain optional per-action overrides. Each request carries a generation
guard, so a newer request stops playback and prevents older HTTP responses from
starting later. TTS has independent Azure host, deployment, API key, optional
two-letter ISO 639-1 language, default voice, and optional instructions
settings. The language contributes speech guidance, while instructions are
passed verbatim to Azure for dialect, accent, or pronunciation guidance.

## Music CD Player

Audio-capable builds with the sound player enabled also provide a bounded Music
CD assembled from canonical MP3 paths discovered recursively under `/media`.
The audio worker owns the immutable catalog, CD-style transport, incremental
decoder session, and elapsed-time accounting. The catalog uses
two PSRAM-backed snapshots: the worker builds an inactive slot and publishes
it, while portal readers copy the active slot under a normal FreeRTOS mutex.
Catalog-sized copies never run under a `portMUX` critical section or on the
audio-task stack. The audio worker remains the only owner of decoder,
resampler, output driver, and I2S path.

Music transport supports Play/Pause, Next, Previous, and Stop. Playback starts
at the first sorted path, does not wrap, and returns to its home position after
Stop, a final track, or a playback failure. Before PCM is emitted for a track,
the decoder opens the file and begins incremental playback immediately. Catalog
discovery reads a bounded MP3 prefix to parse ID3 title, artist, album, and
track metadata plus Xing/Info, VBRI, or CBR-estimated duration. This avoids a
full-file decode before playback; elapsed time advances only after PCM output
is accepted.

Tone Alerts overlay active Music after resampling and before the existing sole
output write. MP3 Alerts are exclusive: they stop Music and use the same
decoder session and output path. Music files can be managed through the portal
only while neither Music nor an MP3 Alert is active.

Music transport and catalog-refresh requests use a dedicated, bounded worker
queue. This keeps them independent from replaceable tone/alert requests, so an
alert cannot discard a pending refresh. Transport submission is non-blocking
and reports busy when the queue is full.

### Demand-Driven Music Analysis

On selected ESP32-P4 audio boards, `HAS_MUSIC_ANALYSIS` enables optional
pre-volume analysis of Music MP3 playback. The existing Music binding exposes
the values through `[music:analysis.*]`:

* `[music:analysis.rms]` and `[music:analysis.peak]` return integer levels from
    0 to 100.
* `[music:analysis.band.0]` through `[music:analysis.band.7]` return eight fixed
    logarithmic visualizer bands.

The feature has two guards. The compile-time flag removes the analyzer from
boards that do not support it. On enabled boards, the display task rebuilds a
demand mask from configured binding consumers. Music playback only reads this
mask when producing PCM and skips analysis when no analysis binding is present.

Spectrum analysis uses a bounded, PSRAM-first 2048-frame mono window and eight
fixed Goertzel filters. It is capped at 20 updates per second and does not add a
general FFT dependency. The small published snapshot is copied under a short
critical section; PCM buffers remain owned by the audio worker.

## Board And Driver Selection

Audio-capable boards select their driver and pin mapping in
`board_overrides.h`. The common defaults and driver identifiers live in
`board_config.h`.

| Board | Output hardware | Driver |
| --- | --- | --- |
| `esp32-p4-lcd4b` | ES8311 codec | `ES8311Driver` |
| `jc4880p433` | ES8311 codec | `ES8311Driver` |
| `jc1060p470c` | ES8311 codec | `ES8311Driver` |
| `jc3636w518` | PCM510xA DAC | `PCM510xADriver` |

The audio sample rate is board-configurable. The current audio boards use
48 kHz output; update the I2S clocking and output-buffer timing together when
changing that rate.

## ES8311 Output

The ES8311 driver configures both I2S transport and codec registers through
I2C. At 48 kHz, its master clock is 12.288 MHz, which is a 256x multiple of
the sample rate. Keep the codec configuration and I2S channel rate aligned.

ES8311 provides hardware volume control, so `setVolume()` programs the codec
rather than scaling each PCM sample in software.

## PCM510xA Output

The PCM510xA has no I2C or SPI control interface. The driver creates a
TX-only I2S channel and controls the external mute pin through `AUDIO_PA_PIN`.

The DAC derives its clocks from BCK and LRCK. Its SCK input is held push-pull
LOW before I2S starts so the DAC PLL can lock. Do not leave SCK floating or
start and stop the clock while a stream is active.

The PCM510xA path applies the shared Q15 gain table in `audio_gain.h` to each
16-bit PCM sample before DMA transfer. The table ranges from silence at volume
0 to unity at volume 100 and must not amplify above unity. Do not replace this
with floating-point gain in the output path.

## I2S Frame Format

PCM samples are 16-bit values carried in 32-bit I2S slots. These width fields
describe different parts of the interface and must agree with that framing:

* `data_bit_width` describes the PCM value and DMA buffer format
* `slot_bit_width` controls the BCK clocks per channel slot
* `ws_width` controls the LRCK high and low duration

For 16-bit data in 32-bit slots, set both `slot_bit_width` and `ws_width` to
32 bits. Changing only the slot width produces an invalid channel frame and
can drop or misplace stereo data.

## Memory And Task Constraints

I2S DMA descriptors and buffers must reside in internal DMA-capable RAM.
PSRAM is not DMA-capable for this transfer path. At 48 kHz, the current TX DMA
payload is $6 \times 240 \times 4 = 5760$ bytes, excluding descriptors and
driver metadata.

MP3 decode and resampling buffers are transient and PSRAM-first. Boards that
enable `AUDIO_MP3_SCRATCH_PSRAM` require PSRAM for decoder scratch space. Keep
large decode buffers out of task stacks, check allocations, and keep output
driver operations limited to I2S and hardware control.

The minimp3 per-frame scratch workspace is roughly 16 KB. Boards with a Music
player and a constrained internal audio stack, including `jc1060p470c`, enable
`AUDIO_MP3_SCRATCH_PSRAM` so decoding uses
`mp3dec_decode_frame_with_scratch()` instead of placing that workspace on the
audio-task stack.

The audio worker stack itself remains in internal RAM because it accesses
flash-backed storage.

The Music catalog is also PSRAM-backed. A catalog retains at most 32 stable,
lexicographically ordered paths; when more files exist, discovery continues,
publishes the first 32, and records overflow metadata. Failed filesystem
refreshes preserve the previously published catalog rather than clearing it.

## MP3 Decode And Resampling

MP3 frames can arrive at several supported source rates. `sound_player.cpp`
resamples interleaved stereo frames to `AUDIO_SAMPLE_RATE` while preserving
state across frame boundaries.

The output-frame capacity is derived from the lowest supported source rate and
the maximum MP3 frame size. It is an exact valid bound, not a warning
threshold. Do not add arbitrary headroom or treat an exact-capacity result as
truncation. When a call ends, carry the fractional position and required
source samples into the next call so adjacent decoded frames remain continuous.
Music uploads are published after path validation and storage completion; the
first decode occurs when the track is opened for playback.

## Diagnostics And Logging

`audio.cpp` reports output starvation from the time represented by queued DMA
frames. This metric identifies whether the output writer met its timing budget;
it does not establish MP3 decode or resampler correctness.

Use one low-rate initialization or state-change log per event. Do not emit
logs from PCM sample loops, DMA write loops, or resampler inner loops. Follow
the project [logging guidelines](logging-guidelines.md) for severity and
formatting.
