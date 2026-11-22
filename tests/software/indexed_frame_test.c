#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "DSPetIndexedFrame.h"

int main(void) {
    const size_t pixels = 240 * 240, capacityBytes = pixels * 2;
    uint8_t     *frame = malloc(capacityBytes + 16);
    assert(frame);
    memset(frame, 0xCC, capacityBytes + 16);
    for (unsigned i = 0; i < 256; ++i) {
        frame[i * 2]     = i;
        frame[i * 2 + 1] = 255 - i;
    }
    for (size_t i = 0; i < pixels; ++i) frame[512 + i] = (i * 53) % 256;
    assert(DSPetIndexedFrameExpand(frame, pixels, capacityBytes));
    for (size_t i = 0; i < pixels; ++i) {
        uint8_t index = (i * 53) % 256;
        assert(frame[i * 2] == index);
        assert(frame[i * 2 + 1] == 255 - index);
    }
    for (size_t i = capacityBytes; i < capacityBytes + 16; ++i) assert(frame[i] == 0xCC);
    assert(!DSPetIndexedFrameExpand(NULL, pixels, capacityBytes));
    assert(!DSPetIndexedFrameExpand(frame, pixels, capacityBytes - 1));
    assert(!DSPetIndexedFrameExpand(frame, 5, 516));
    uint8_t payload[516] = {0};
    uint8_t previous[4]  = {100, 100, 100, 100};
    memcpy(payload + 512, (uint8_t[]) {1, 2, 3, 4}, 4);
    DSPetIndexedFrameRestoreDelta(payload, previous, 4, true);
    assert(memcmp(previous, (uint8_t[]) {1, 2, 3, 4}, 4) == 0);
    memcpy(payload + 512, (uint8_t[]) {0, 3, 2, 5}, 4);
    DSPetIndexedFrameRestoreDelta(payload, previous, 4, false);
    assert(memcmp(previous, (uint8_t[]) {1, 1, 1, 1}, 4) == 0);
    assert(memcmp(payload + 512, previous, 4) == 0);
    memcpy(payload + 512, (uint8_t[]) {7, 8, 9, 10}, 4);
    DSPetIndexedFrameRestoreDelta(payload, previous, 4, true);
    assert(memcmp(previous, (uint8_t[]) {7, 8, 9, 10}, 4) == 0);
    free(frame);
    return 0;
}
