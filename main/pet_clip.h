#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *name;
    const uint8_t *start;
    const uint8_t *end;
    const uint32_t *offsets; // frame_count + 1 entries; last is the total size
    unsigned frame_count;
    unsigned fps;
    unsigned keyframe_interval;
} pet_clip_t;
