#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Input: 512-byte RGB565_BE palette + pixel_count 8-bit indices.
// Output: pixel_count RGB565_BE pixels in the same buffer.
bool indexed_frame_expand(uint8_t *buffer, size_t pixel_count, size_t capacity);
// Restore XOR indices using the preceding frame, then cache them for the next.
void indexed_frame_restore_delta(uint8_t *payload, uint8_t *previous,
                                 size_t pixel_count, bool keyframe);
