#include "DSPetButtonFilter.h"
#include <assert.h>
#include <stdint.h>

int main(void)
{
    DSPetButtonFilter f;
    DSPetButtonFilterInit(&f, false, 0);
    // Contact bounce must produce exactly one press after 30 ms stable.
    assert(!DSPetButtonFilterUpdate(&f, true, 10));
    assert(!DSPetButtonFilterUpdate(&f, false, 15));
    assert(!DSPetButtonFilterUpdate(&f, true, 20));
    assert(!DSPetButtonFilterUpdate(&f, true, 49));
    assert(DSPetButtonFilterUpdate(&f, true, 50));
    // Holding the button does not repeat.
    assert(!DSPetButtonFilterUpdate(&f, true, 5000));
    // A release that bounces back low must not re-arm.
    assert(!DSPetButtonFilterUpdate(&f, false, 5010));
    assert(!DSPetButtonFilterUpdate(&f, true, 5020));
    assert(!DSPetButtonFilterUpdate(&f, true, 5050));
    assert(!DSPetButtonFilterUpdate(&f, false, 5060));
    assert(!DSPetButtonFilterUpdate(&f, false, 5090));
    assert(!DSPetButtonFilterUpdate(&f, true, 5100));
    assert(DSPetButtonFilterUpdate(&f, true, 5130));
    // A button held during startup is ignored until released and pressed again.
    DSPetButtonFilterInit(&f, true, 0);
    assert(!DSPetButtonFilterUpdate(&f, true, 100));
    assert(!DSPetButtonFilterUpdate(&f, false, 200));
    assert(!DSPetButtonFilterUpdate(&f, false, 230));
    assert(!DSPetButtonFilterUpdate(&f, true, 240));
    assert(DSPetButtonFilterUpdate(&f, true, 270));
    // Debouncing is valid across a uint32 millisecond clock wrap.
    DSPetButtonFilterInit(&f, false, UINT32_MAX - 20);
    assert(!DSPetButtonFilterUpdate(&f, true, UINT32_MAX - 10));
    assert(!DSPetButtonFilterUpdate(&f, true, 18));
    assert(DSPetButtonFilterUpdate(&f, true, 19));
    return 0;
}
