#include "DSPetSpeechDetector.h"

#include <math.h>
#include <stdlib.h>

#include "DSPetSpeechCapture.h"
#include "esp_vad.h"

enum {
    kSampleRateHz    = 16000,
    kFrameMs         = 20,
    kVadGain         = 8,
    kBargeOnsetRms   = 110,
    kListenOnsetRms  = 90,
    kEnergyRms       = 52,
    kContinuationRms = 48,
    kMeterPeak       = 384,
};

struct DSPetSpeechDetector {
    vad_handle_t vad;
    int16_t      amplified[kDSPetSpeechFrameSamples];
    float        quietRms;
};

esp_err_t DSPetSpeechDetectorCreate(DSPetSpeechDetector **out) {
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out                          = NULL;
    DSPetSpeechDetector *detector = calloc(1, sizeof(*detector));
    if (!detector) {
        return ESP_ERR_NO_MEM;
    }
    detector->vad = vad_create(VAD_MODE_2);
    if (!detector->vad) {
        free(detector);
        return ESP_ERR_NO_MEM;
    }
    *out = detector;
    return ESP_OK;
}

DSPetSpeechEvidence DSPetSpeechDetectorProcess(
    DSPetSpeechDetector *detector, const int16_t *pcm, bool recording, bool playing, float *rms) {
    uint32_t squares = 0;
    for (size_t i = 0; i < kDSPetSpeechFrameSamples; ++i) {
        int32_t magnitude = abs(pcm[i]);
        int32_t metered   = magnitude < kMeterPeak ? magnitude : kMeterPeak;
        squares += (uint32_t) (metered * metered);
        int32_t sample = (int32_t) pcm[i] * kVadGain;
        if (sample < INT16_MIN) {
            sample = INT16_MIN;
        } else if (sample > INT16_MAX) {
            sample = INT16_MAX;
        }
        detector->amplified[i] = (int16_t) sample;
    }
    *rms             = sqrtf((float) squares / kDSPetSpeechFrameSamples);
    const bool voice = vad_process(detector->vad, detector->amplified, kSampleRateHz, kFrameMs) == VAD_SPEECH;
    if (recording) {
        // 用开口前的安静底噪判断停顿。保留较强但被 VAD 漏判的辅音。
        const float floor = fmaxf(kContinuationRms, detector->quietRms * 1.5f);
        return *rms >= floor && (voice || *rms >= kListenOnsetRms) ? kDSPetSpeechVoiced : kDSPetSpeechSilent;
    }
    if (*rms < kEnergyRms) {
        detector->quietRms += (*rms - detector->quietRms) * 0.05f;
    }
    // WebRTC 的保持状态仍持续更新，能量门限避免一个孤立敲击延长命中。
    if (!voice || *rms < kEnergyRms) {
        return kDSPetSpeechSilent;
    }
    return *rms >= (playing ? kBargeOnsetRms : kListenOnsetRms) ? kDSPetSpeechStrong : kDSPetSpeechVoiced;
}

void DSPetSpeechDetectorDestroy(DSPetSpeechDetector *detector) {
    if (detector) {
        vad_destroy(detector->vad);
        free(detector);
    }
}
