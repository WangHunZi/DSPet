#include <assert.h>

#include "DSPetAudioCorrelation.h"

enum { kReferenceSamples = 256, kMicSamples = 640, kOffset = 137 };

int main(void) {
    int16_t  reference[kReferenceSamples];
    int16_t  mic[kMicSamples];
    uint32_t noise = 7;
    for (size_t i = 0; i < kMicSamples; ++i) {
        noise  = noise * 1664525u + 1013904223u;
        mic[i] = 230 + (int16_t) (noise >> 26) - 32;
    }
    for (size_t i = 0; i < kReferenceSamples; ++i) {
        noise        = noise * 1664525u + 1013904223u;
        reference[i] = 1000 + (int16_t) (noise >> 21);
        // 测量路径可以反相，而且麦克风带有直流偏置和背景噪声。
        mic[kOffset + i] -= reference[i] / 2;
    }

    DSPetAudioCorrelation match;
    assert(DSPetAudioCorrelationFind(reference, kReferenceSamples, mic, kMicSamples, 90, 180, &match));
    assert(match.offset == kOffset);
    assert(match.score > 0.98f);

    for (size_t i = 0; i < kMicSamples; ++i) {
        mic[i] = 230;
    }
    assert(!DSPetAudioCorrelationFind(reference, kReferenceSamples, mic, kMicSamples, 90, 180, &match));
    assert(!DSPetAudioCorrelationFind(reference, kReferenceSamples, mic, kMicSamples, 90, 500, &match));
    return 0;
}
