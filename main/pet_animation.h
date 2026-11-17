#pragma once

// Play flash-resident clips, switching on BOOT presses, on the calling task.
// Call only after display_init() and button_start().
void pet_play(void);
