#include "DSPetLoopbackTest.h"

#include <stdio.h>

#include "DSPetAudioCheck.h"

esp_err_t DSPetLoopbackTestRun(void) {
    puts("听到提示音后说两三秒，然后听喇叭回放。");
    return DSPetAudioCheckRun();
}
