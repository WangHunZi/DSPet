#include "DSPetButtonFilter.h"

void DSPetButtonFilterInit(DSPetButtonFilter *filter, bool pressed, uint32_t nowMs) {
    *filter = (DSPetButtonFilter) {
        .rawPressed    = pressed,
        .stablePressed = pressed,
        .armed         = !pressed,
        .changedAtMs   = nowMs,
    };
}

bool DSPetButtonFilterUpdate(DSPetButtonFilter *filter, bool pressed, uint32_t nowMs) {
    if (pressed != filter->rawPressed) {
        filter->rawPressed  = pressed;
        filter->changedAtMs = nowMs;
    }
    // 无符号差值允许毫秒时钟回绕。
    if ((uint32_t) (nowMs - filter->changedAtMs) < 30 || filter->stablePressed == pressed) return false;
    filter->stablePressed = pressed;
    if (!pressed) {
        filter->armed = true;
        return false;
    }
    if (!filter->armed) return false;
    filter->armed = false;
    return true;
}
