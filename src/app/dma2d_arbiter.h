#pragma once

#include <stdint.h>

// ESP32-P4 shares one 2D-DMA between the hardware JPEG codec and the display
// PPA/DMA2D flush path. Overlapping transfers deadlock both engines, so every
// user holds this token for the duration of its hardware transfer.
void dma2d_arbiter_init();
bool dma2d_arbiter_acquire(uint32_t timeout_ms);
void dma2d_arbiter_release();
void dma2d_arbiter_release_from_isr();
