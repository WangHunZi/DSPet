#include "action_state.h"
#include <assert.h>

bool action_state_update(action_state_t *state, unsigned clip_count, uint32_t presses,
                         int64_t now_us, int64_t duration_us, bool automatic)
{
    assert(clip_count > 0 && duration_us > 0);
    uint32_t steps = presses - state->last_presses;
    state->last_presses = presses;
    if (!steps) {
        if (!automatic || now_us - state->started_us < duration_us) return false;
        steps = 1;
    }
    state->index = (state->index + steps % clip_count) % clip_count;
    state->started_us = now_us;
    return true;
}
