#include "DSPetActionState.h"
#include <assert.h>

bool DSPetActionStateUpdate(DSPetActionState *state, unsigned clipCount, uint32_t presses,
                         int64_t nowUs, int64_t durationUs, bool automatic)
{
    assert(clipCount > 0 && durationUs > 0);
    uint32_t steps = presses - state->lastPresses;
    state->lastPresses = presses;
    if (!steps) {
        if (!automatic || nowUs - state->startedUs < durationUs) return false;
        steps = 1;
    }
    state->index = (state->index + steps % clipCount) % clipCount;
    state->startedUs = nowUs;
    return true;
}
