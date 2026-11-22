#include <assert.h>
#include <limits.h>
#include <string.h>

#include "DSPetPcm24kTo16k.h"
#include "DSPetPcmBufferPolicy.h"
#include "DSPetPcmWav.h"

int main(void) {
    DSPetPcm24kTo16k state = {0};
    int16_t          output[2];
    assert(DSPetPcm24kTo16kPush(&state, 6, output) == 0);
    assert(DSPetPcm24kTo16kPush(&state, 12, output) == 0);
    assert(DSPetPcm24kTo16kPush(&state, 24, output) == 2);
    assert(output[0] == 8 && output[1] == 20);

    for (int i = 0; i < 3; ++i) {
        assert(DSPetPcm24kTo16kPush(&state, INT16_MIN, output) == (i == 2 ? 2 : 0));
    }
    assert(output[0] == INT16_MIN && output[1] == INT16_MIN);

    const uint8_t pcm[] = {6, 0, 12, 0, 24, 0};
    assert(DSPetPcm24kTo16kDecode(&state, pcm, 1, output) == 0);
    assert(DSPetPcm24kTo16kDecode(&state, pcm + 1, 3, output) == 0);
    assert(DSPetPcm24kTo16kDecode(&state, pcm + 4, 2, output) == 2);
    assert(output[0] == 8 && output[1] == 20 && !state.haveLowByte);

    const uint8_t expected[] = {
        'R', 'I', 'F',  'F',  42, 0, 0,    0,    'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16,  0,   0, 0, 1, 0,
        1,   0,   0x80, 0x3e, 0,  0, 0x00, 0x7d, 0,   0,   2,   0,   16,  0,   'd', 'a', 't', 'a', 6, 0, 0, 0,
    };
    uint8_t header[kDSPetPcmWavHeaderBytes];
    DSPetPcmWavHeader(header, 16000, 3);
    assert(sizeof(expected) == sizeof(header) && memcmp(header, expected, sizeof(header)) == 0);
    const size_t capacity = 2 * 1024 * 1024;
    assert(!DSPetPcmBufferCanStart(0, capacity, 0, true));
    assert(!DSPetPcmBufferCanStart(32000, capacity, 1000, false));
    assert(DSPetPcmBufferCanStart(32000, capacity, 4000, true));
    assert(!DSPetPcmBufferCanStart(96000, capacity, 7000, false));
    assert(DSPetPcmBufferCanStart(96000, capacity, 2000, false));
    assert(DSPetPcmBufferCanStart(capacity - 32000, capacity, 120000, false));
    assert(!DSPetPcmBufferCanResume(64000, capacity, false));
    assert(DSPetPcmBufferCanResume(64000, capacity, true));
    return 0;
}
