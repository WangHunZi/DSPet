#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    unsigned index;
    uint32_t last_presses;
    int64_t started_us;
} action_state_t;

// Manual presses take priority over expiry on the same tick.
bool action_state_update(action_state_t *state, unsigned clip_count, uint32_t presses,
                         int64_t now_us, int64_t duration_us, bool automatic);
