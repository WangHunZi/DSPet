#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 240
#define DISPLAY_FRAME_BYTES (DISPLAY_WIDTH * DISPLAY_HEIGHT * 2)

// Initialize once at boot. All calls must come from the same display task.
void display_init(void);
void display_fill(uint16_t rgb565);
// Full frame, row-major RGB565, most significant byte first.
// Copies flash pixels to internal DMA RAM and waits for completion.
esp_err_t display_present(const uint8_t *pixels, size_t size);
