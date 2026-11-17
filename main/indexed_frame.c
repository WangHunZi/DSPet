#include "indexed_frame.h"
#include <string.h>

void indexed_frame_restore_delta(uint8_t *payload, uint8_t *previous,
                                 size_t pixel_count, bool keyframe)
{
    uint8_t *indices = payload + 512;
    if (!keyframe)
        for (size_t i = 0; i < pixel_count; ++i) indices[i] ^= previous[i];
    memcpy(previous, indices, pixel_count);
}

bool indexed_frame_expand(uint8_t *buffer, size_t pixel_count, size_t capacity)
{
    if (!buffer || capacity < 512 || pixel_count > capacity / 2 ||
        pixel_count > capacity - 512) return false;
    uint8_t palette[512];
    memcpy(palette, buffer, sizeof(palette));
    // Align indices with the end of the output buffer. Expanding forward
    // then writes only behind the unread source, including the first 512 pixels.
    uint8_t *indices = buffer + capacity - pixel_count;
    memmove(indices, buffer + sizeof(palette), pixel_count);
    for (size_t i = 0; i < pixel_count; ++i) {
        unsigned color = indices[i] * 2;
        buffer[i * 2] = palette[color];
        buffer[i * 2 + 1] = palette[color + 1];
    }
    return true;
}
