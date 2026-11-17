#pragma once

#include <stdbool.h>
#include <stdint.h>

// Start one GPIO0/BOOT polling task. Call once, before the animation loop.
void button_start(void);
// Single consumer; returns the latest cumulative press count if it changed.
bool button_take_count(uint32_t *count);
