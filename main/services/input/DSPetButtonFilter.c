#include "DSPetButtonFilter.h"

enum { kDebounceMs = 30, kLongPressMs = 5000 };

void DSPetButtonFilterInit(DSPetButtonFilter *filter, bool pressed, uint32_t nowMs) {
    *filter = (DSPetButtonFilter) {
        .rawPressed    = pressed,
        .stablePressed = pressed,
        .armed         = !pressed,
        .changedAtMs   = nowMs,
    };
}

DSPetButtonEvent DSPetButtonFilterUpdate(DSPetButtonFilter *filter, bool pressed, uint32_t nowMs) {
    if (pressed != filter->rawPressed) {
        filter->rawPressed  = pressed;
        filter->changedAtMs = nowMs;
    }
    // 无符号差值允许毫秒时钟回绕。
    if (nowMs - filter->changedAtMs < kDebounceMs || filter->stablePressed == pressed) {
        return kDSPetButtonNoEvent;
    }
    filter->stablePressed = pressed;
    if (pressed) {
        filter->pressedAtMs = filter->changedAtMs;
        return kDSPetButtonNoEvent;
    }
    if (!filter->armed) {
        filter->armed = true;
        return kDSPetButtonNoEvent;
    }
    return filter->changedAtMs - filter->pressedAtMs >= kLongPressMs ? kDSPetButtonLongPress : kDSPetButtonShortPress;
}
