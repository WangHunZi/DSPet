#include <assert.h>
#include <limits.h>

#include "DSPetPcm24kTo16k.h"

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
    return 0;
}
