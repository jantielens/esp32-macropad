---
description: "Audio subsystem conventions for output drivers, I2S format, memory, and MP3 playback"
applyTo: "**/audio.cpp, **/audio.h, **/audio_output_driver.h, **/audio_output_drivers.cpp, **/sound_player.cpp, **/sound_player.h, **/drivers/es8311_audio_driver.cpp, **/drivers/es8311_audio_driver.h, **/drivers/pcm510xa_audio_driver.cpp, **/drivers/pcm510xa_audio_driver.h, **/drivers/audio_gain.h, **/board_config.h, **/board_overrides.h"
---

# Audio Subsystem Conventions

## Ownership

* Keep audio commands, worker lifecycle, volume state, and starvation timing in `audio.cpp`.
* Keep MP3 decode, resampling, and PCM frame production in `sound_player.cpp`.
* Add output hardware through `AudioOutputDriver` and select one driver in `audio_output_drivers.cpp`.
* Keep board pin assignments, output-driver selection, and rate overrides in board configuration.

## I2S And Clocking

* Keep `data_bit_width`, `slot_bit_width`, and `ws_width` consistent with the physical frame format.
* For 16-bit PCM carried in 32-bit slots, set both `slot_bit_width` and `ws_width` to 32 bits. Never override only the slot width.
* Keep ES8311 I2S timing and codec clock registers aligned; 48 kHz output uses a 12.288 MHz, 256x MCLK.
* Hold PCM510xA SCK push-pull LOW before I2S startup so its PLL can lock. Never leave SCK floating or start and stop its clock mid-stream.

## Memory And Gain

* Allocate I2S DMA buffers and descriptors in internal DMA-capable RAM. Never place I2S DMA in PSRAM.
* Use PSRAM-first transient buffers for MP3 decode and resampling when available; honor boards that require PSRAM decoder scratch.
* Use the shared Q15 gain helper for PCM510xA software volume. Do not add floating-point gain or amplification above unity in the output path.

## Resampling And Logging

* Derive the resampler output capacity from supported source-rate and MP3-frame bounds. Do not add arbitrary cap headroom.
* Preserve resampler fractional position and carry-over samples across calls. Treat exact output capacity as valid unless a real clamp occurred.
* Keep logs out of PCM, DMA, and resampler inner loops. Log only low-rate initialization, state changes, and actionable failures.
