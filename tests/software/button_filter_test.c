#include <assert.h>

#include "DSPetButtonFilter.h"

int main(void) {
    DSPetButtonFilter filter;
    DSPetButtonFilterInit(&filter, false, 0);
    assert(DSPetButtonFilterUpdate(&filter, true, 10) == kDSPetButtonNoEvent);
    assert(DSPetButtonFilterUpdate(&filter, false, 15) == kDSPetButtonNoEvent);
    assert(DSPetButtonFilterUpdate(&filter, true, 20) == kDSPetButtonNoEvent);
    assert(DSPetButtonFilterUpdate(&filter, true, 50) == kDSPetButtonNoEvent);
    assert(DSPetButtonFilterUpdate(&filter, false, 75) == kDSPetButtonNoEvent);
    assert(DSPetButtonFilterUpdate(&filter, true, 80) == kDSPetButtonNoEvent);
    assert(DSPetButtonFilterUpdate(&filter, false, 90) == kDSPetButtonNoEvent);
    assert(DSPetButtonFilterUpdate(&filter, false, 120) == kDSPetButtonShortPress);
    assert(DSPetButtonFilterUpdate(&filter, false, 121) == kDSPetButtonNoEvent);

    assert(DSPetButtonFilterUpdate(&filter, true, 200) == kDSPetButtonNoEvent);
    assert(DSPetButtonFilterUpdate(&filter, true, 230) == kDSPetButtonNoEvent);
    assert(DSPetButtonFilterUpdate(&filter, true, 5210) == kDSPetButtonNoEvent);
    assert(DSPetButtonFilterUpdate(&filter, false, 5230) == kDSPetButtonNoEvent);
    assert(DSPetButtonFilterUpdate(&filter, false, 5260) == kDSPetButtonLongPress);
    assert(DSPetButtonFilterUpdate(&filter, false, 5261) == kDSPetButtonNoEvent);

    DSPetButtonFilterInit(&filter, true, 0);
    assert(DSPetButtonFilterUpdate(&filter, true, 6000) == kDSPetButtonNoEvent);
    assert(DSPetButtonFilterUpdate(&filter, false, 6100) == kDSPetButtonNoEvent);
    assert(DSPetButtonFilterUpdate(&filter, false, 6130) == kDSPetButtonNoEvent);

    DSPetButtonFilterInit(&filter, false, UINT32_MAX - 60);
    assert(DSPetButtonFilterUpdate(&filter, true, UINT32_MAX - 50) == kDSPetButtonNoEvent);
    assert(DSPetButtonFilterUpdate(&filter, true, UINT32_MAX - 20) == kDSPetButtonNoEvent);
    assert(DSPetButtonFilterUpdate(&filter, false, 4950) == kDSPetButtonNoEvent);
    assert(DSPetButtonFilterUpdate(&filter, false, 4980) == kDSPetButtonLongPress);
}
