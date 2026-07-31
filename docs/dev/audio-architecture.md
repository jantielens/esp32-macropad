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

## MP3 Decode And Resampling

MP3 frames can arrive at several supported source rates. `sound_player.cpp`
resamples interleaved stereo frames to `AUDIO_SAMPLE_RATE` while preserving
state across frame boundaries.

The output-frame capacity is derived from the lowest supported source rate and
the maximum MP3 frame size. It is an exact valid bound, not a warning
threshold. Do not add arbitrary headroom or treat an exact-capacity result as
truncation. When a call ends, carry the fractional position and required
source samples into the next call so adjacent decoded frames remain continuous.

## Diagnostics And Logging

`audio.cpp` reports output starvation from the time represented by queued DMA
frames. This metric identifies whether the output writer met its timing budget;
it does not establish MP3 decode or resampler correctness.

Use one low-rate initialization or state-change log per event. Do not emit
logs from PCM sample loops, DMA write loops, or resampler inner loops. Follow
the project [logging guidelines](logging-guidelines.md) for severity and
formatting.
