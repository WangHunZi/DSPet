#include "action_state.h"
#include <assert.h>
#include <stdint.h>

int main(void)
{
    action_state_t state = {0};
    assert(!action_state_update(&state, 6, 0, 999, 1000, true));
    assert(action_state_update(&state, 6, 0, 1000, 1000, true));
    assert(state.index == 1);
    // Manual switch before expiry resets the next automatic deadline.
    assert(action_state_update(&state, 6, 1, 1200, 1000, true));
    assert(state.index == 2);
    assert(!action_state_update(&state, 6, 1, 2000, 1000, true));
    // Manual press exactly at expiry advances once, not twice.
    assert(action_state_update(&state, 6, 2, 2200, 1000, true));
    assert(state.index == 3);
    // Coalesced rapid presses preserve the final position across auto changes.
    assert(action_state_update(&state, 6, 5, 2300, 1000, true));
    assert(state.index == 0);
    assert(!action_state_update(&state, 6, 5, 9999, 1000, false));
    assert(action_state_update(&state, 6, 6, 10000, 1000, false));
    assert(state.index == 1);
    // Cumulative press counter wrapping is one additional press.
    state.last_presses = UINT32_MAX;
    assert(action_state_update(&state, 6, 0, 10001, 1000, false));
    assert(state.index == 2);
    return 0;
}
