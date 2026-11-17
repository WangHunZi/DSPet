#include "button_filter.h"

void button_filter_init(button_filter_t *filter, bool pressed, uint32_t now_ms)
{
    *filter = (button_filter_t) {
        .raw_pressed = pressed, .stable_pressed = pressed,
        .armed = !pressed, .changed_at_ms = now_ms,
    };
}

bool button_filter_update(button_filter_t *filter, bool pressed, uint32_t now_ms)
{
    if (pressed != filter->raw_pressed) {
        filter->raw_pressed = pressed;
        filter->changed_at_ms = now_ms;
    }
    // Unsigned subtraction also handles the millisecond clock wrapping.
    if ((uint32_t)(now_ms - filter->changed_at_ms) < 30 ||
        filter->stable_pressed == pressed) return false;
    filter->stable_pressed = pressed;
    if (!pressed) {
        filter->armed = true;
        return false;
    }
    if (!filter->armed) return false;
    filter->armed = false;
    return true;
}
