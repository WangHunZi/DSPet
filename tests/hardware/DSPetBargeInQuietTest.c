#include "DSPetBargeInQuietTest.h"

#include <stdio.h>

#include "DSPetBargeIn.h"

extern const int16_t dspetPlaybackDemoStart[];
extern const int16_t dspetPlaybackDemoEnd[];

esp_err_t DSPetBargeInQuietTestRun(void) {
    puts("安静播报测试即将开始，请保持安静。");
    size_t samples = ((uintptr_t) dspetPlaybackDemoEnd - (uintptr_t) dspetPlaybackDemoStart) / sizeof(int16_t);
    DSPetBargeInResult result;
    esp_err_t          error = DSPetBargeInRun(dspetPlaybackDemoStart, samples, false, &result);
    printf("DSPET_TEST_METRIC barge_in_quiet_interrupted=%u natural_end=%u lost=%u peak_rms=%.1f\n",
           (unsigned) result.interrupted,
           (unsigned) result.naturalEnd,
           result.lostBlocks,
           result.peakRms);
    if (error != ESP_OK) {
        return error;
    }
    return result.naturalEnd && !result.interrupted ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}
