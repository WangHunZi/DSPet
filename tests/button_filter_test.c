#include "button_filter.h"
#include <assert.h>
#include <stdint.h>

int main(void)
{
    button_filter_t f;
    button_filter_init(&f, false, 0);
    // Contact bounce must produce exactly one press after 30 ms stable.
    assert(!button_filter_update(&f, true, 10));
    assert(!button_filter_update(&f, false, 15));
    assert(!button_filter_update(&f, true, 20));
    assert(!button_filter_update(&f, true, 49));
    assert(button_filter_update(&f, true, 50));
    // Holding the button does not repeat.
    assert(!button_filter_update(&f, true, 5000));
    // A release that bounces back low must not re-arm.
    assert(!button_filter_update(&f, false, 5010));
    assert(!button_filter_update(&f, true, 5020));
    assert(!button_filter_update(&f, true, 5050));
    assert(!button_filter_update(&f, false, 5060));
    assert(!button_filter_update(&f, false, 5090));
    assert(!button_filter_update(&f, true, 5100));
    assert(button_filter_update(&f, true, 5130));
    // A button held during startup is ignored until released and pressed again.
    button_filter_init(&f, true, 0);
    assert(!button_filter_update(&f, true, 100));
    assert(!button_filter_update(&f, false, 200));
    assert(!button_filter_update(&f, false, 230));
    assert(!button_filter_update(&f, true, 240));
    assert(button_filter_update(&f, true, 270));
    // Debouncing is valid across a uint32 millisecond clock wrap.
    button_filter_init(&f, false, UINT32_MAX - 20);
    assert(!button_filter_update(&f, true, UINT32_MAX - 10));
    assert(!button_filter_update(&f, true, 18));
    assert(button_filter_update(&f, true, 19));
    return 0;
}
