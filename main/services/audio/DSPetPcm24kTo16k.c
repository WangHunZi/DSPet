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
