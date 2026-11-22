#include "DSPetPcm24kTo16k.h"

size_t DSPetPcm24kTo16kPush(DSPetPcm24kTo16k *state, int16_t sample, int16_t output[2]) {
    state->samples[state->used++] = sample;
    if (state->used != 3) {
        return 0;
    }
    const int32_t middle = state->samples[1];
    output[0]            = (int16_t) ((2 * (int32_t) state->samples[0] + middle) / 3);
    output[1]            = (int16_t) ((middle + 2 * (int32_t) state->samples[2]) / 3);
    state->used          = 0;
    return 2;
}

size_t DSPetPcm24kTo16kDecode(DSPetPcm24kTo16k *state, const uint8_t *bytes, size_t byteCount, int16_t *output) {
    size_t count = 0;
    for (size_t i = 0; i < byteCount; ++i) {
        if (!state->haveLowByte) {
            state->lowByte     = bytes[i];
            state->haveLowByte = true;
        } else {
            const int16_t sample = (int16_t) ((uint16_t) bytes[i] << 8 | state->lowByte);
            state->haveLowByte   = false;
            count += DSPetPcm24kTo16kPush(state, sample, output + count);
        }
    }
    return count;
}
