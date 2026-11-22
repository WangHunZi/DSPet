#include "DSPetPcmReplay.h"

#include <math.h>
#include <stdlib.h>

enum { kReplayPeak = 2048, kGainScale = 256, kTargetRms = 512, kMeterPeak = 512, kMaxGain = 16 };

bool DSPetPcmReplayNormalize(int16_t *pcm, size_t sampleCount, DSPetPcmReplayLevel *level) {
    if (!pcm || !sampleCount || !level) {
        return false;
    }
    int64_t sum = 0;
    for (size_t i = 0; i < sampleCount; ++i) {
        sum += pcm[i];
    }
    int32_t dc          = (int32_t) (sum / (int64_t) sampleCount);
    int32_t peak        = 0;
    int64_t energy      = 0;
    int64_t meterEnergy = 0;
    for (size_t i = 0; i < sampleCount; ++i) {
        int32_t sample    = pcm[i] - dc;
        int32_t magnitude = abs(sample);
        if (magnitude > peak) {
            peak = magnitude;
        }
        energy += (int64_t) sample * sample;
        int32_t metered = magnitude < kMeterPeak ? magnitude : kMeterPeak;
        meterEnergy += (int64_t) metered * metered;
    }
    double meteredRms = sqrt((double) meterEnergy / sampleCount);
    double ratio      = meteredRms >= (double) kTargetRms / kMaxGain ? kTargetRms / meteredRms : kMaxGain;
    *level            = (DSPetPcmReplayLevel) {
        .dc      = dc,
        .peak    = peak,
        .rms     = sqrt((double) energy / sampleCount),
        .metered = meteredRms,
        .gain    = ratio,
    };
    if (!peak) {
        return false;
    }

    // 用截幅 RMS 估音量。实际输出单独限幅，避免尖峰主导整段增益。
    int32_t gain = (int32_t) (ratio * kGainScale);
    for (size_t i = 0; i < sampleCount; ++i) {
        int32_t sample = (pcm[i] - dc) * gain / kGainScale;
        if (sample > kReplayPeak) {
            sample = kReplayPeak;
        } else if (sample < -kReplayPeak) {
            sample = -kReplayPeak;
        }
        pcm[i] = (int16_t) sample;
    }
    return true;
}
