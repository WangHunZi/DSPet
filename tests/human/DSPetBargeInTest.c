#include "DSPetBargeInTest.h"

#include <inttypes.h>
#include <stdio.h>

#include "DSPetBargeIn.h"

extern const int16_t dspetPlaybackDemoStart[];
extern const int16_t dspetPlaybackDemoEnd[];

static esp_err_t run(bool expectSpeech) {
    size_t samples = ((uintptr_t) dspetPlaybackDemoEnd - (uintptr_t) dspetPlaybackDemoStart) / sizeof(int16_t);
    DSPetBargeInResult result;
    esp_err_t          error = DSPetBargeInRun(dspetPlaybackDemoStart, samples, expectSpeech, &result);
    printf("DSPET_TEST_METRIC barge_in_interrupted=%u natural_end=%u recorded_samples=%u preroll_samples=%u"
           " cancel_to_drain_us=%" PRId64 " lost=%u peak_rms=%.1f\n",
           (unsigned) result.interrupted,
           (unsigned) result.naturalEnd,
           (unsigned) result.recordedSamples,
           (unsigned) result.preRollSamples,
           result.drainUs,
           result.lostBlocks,
           result.peakRms);
    if (error != ESP_OK) {
        return error;
    }
    bool passed = expectSpeech ? result.interrupted && result.recordedSamples > result.preRollSamples
                               : result.naturalEnd && !result.interrupted;
    return passed ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t DSPetBargeInSpeechTestRun(void) {
    puts("播报中途直接说「等一下，我问的是明天」，说完整句，稍后听回放。");
    return run(true);
}

esp_err_t DSPetBargeInKeyboardTestRun(void) {
    puts("播报开始约两秒后正常打字几秒，保持不说话。");
    return run(false);
}
