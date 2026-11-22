#include "DSPetLoopbackTest.h"
#include "DSPetAudioCheck.h"
#include <stdio.h>

esp_err_t DSPetLoopbackTestRun(void)
{
    puts("听到提示音后说两三秒，然后听喇叭回放。");
    return DSPetAudioCheckRun();
}
