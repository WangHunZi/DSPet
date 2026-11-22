#include "DSPetPcmWav.h"

#include <string.h>

static void put32(uint8_t *bytes, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        bytes[i] = (uint8_t) (value >> (i * 8));
    }
}

void DSPetPcmWavHeader(uint8_t header[kDSPetPcmWavHeaderBytes], uint32_t sampleRateHz, uint32_t sampleCount) {
    static const uint8_t pattern[kDSPetPcmWavHeaderBytes] = {
        'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16,  0,   0, 0, 1, 0,
        1,   0,   0,   0,   0, 0, 0, 0, 0,   0,   2,   0,   16,  0,   'd', 'a', 't', 'a', 0, 0, 0, 0,
    };
    memcpy(header, pattern, sizeof(pattern));
    put32(header + 4, 36 + 2 * sampleCount);
    put32(header + 24, sampleRateHz);
    put32(header + 28, 2 * sampleRateHz);
    put32(header + 40, 2 * sampleCount);
}
