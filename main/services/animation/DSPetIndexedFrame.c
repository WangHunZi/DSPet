#include "DSPetIndexedFrame.h"

#include <string.h>

void DSPetIndexedFrameRestoreDelta(uint8_t *payload, uint8_t *previous, size_t pixelCount, bool keyframe) {
    uint8_t *indices = payload + kDSPetIndexedFramePaletteBytes;
    if (!keyframe)
        for (size_t i = 0; i < pixelCount; ++i) indices[i] ^= previous[i];
    memcpy(previous, indices, pixelCount);
}

bool DSPetIndexedFrameExpand(uint8_t *buffer, size_t pixelCount, size_t capacityBytes) {
    if (!buffer || capacityBytes < kDSPetIndexedFramePaletteBytes || pixelCount > capacityBytes / 2 ||
        pixelCount > capacityBytes - kDSPetIndexedFramePaletteBytes)
        return false;
    uint8_t palette[kDSPetIndexedFramePaletteBytes];
    memcpy(palette, buffer, sizeof(palette));
    // Align indices with the end of the output buffer. Expanding forward
    // then writes only behind the unread source, including the first kDSPetIndexedFramePaletteBytes pixels.
    uint8_t *indices = buffer + capacityBytes - pixelCount;
    memmove(indices, buffer + sizeof(palette), pixelCount);
    for (size_t i = 0; i < pixelCount; ++i) {
        unsigned color    = indices[i] * 2;
        buffer[i * 2]     = palette[color];
        buffer[i * 2 + 1] = palette[color + 1];
    }
    return true;
}
