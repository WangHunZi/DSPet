#include "DSPetSpeechDetector.h"

#include <math.h>
#include <stdlib.h>

#include "DSPetSpeechCapture.h"
#include "esp_vad.h"

enum {
    kSampleRateHz = 16000,
    kFrameMs      = 20,
    kVadGain      = 8,
    kEnergyRms    = 52,
    kMeterPeak    = 384,
};

struct DSPetSpeechDetector {
    vad_handle_t vad;
    int16_t      amplified[kDSPetSpeechFrameSamples];
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

bool DSPetSpeechDetectorProcess(DSPetSpeechDetector *detector, const int16_t *pcm, float *rms) {
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
    *rms = sqrtf((float) squares / kDSPetSpeechFrameSamples);
    // WebRTC 的保持状态仍持续更新，能量门限避免一个孤立敲击延长命中。
    return vad_process(detector->vad, detector->amplified, kSampleRateHz, kFrameMs) == VAD_SPEECH &&
           squares >= kEnergyRms * kEnergyRms * kDSPetSpeechFrameSamples;
}

void DSPetSpeechDetectorDestroy(DSPetSpeechDetector *detector) {
    if (detector) {
        vad_destroy(detector->vad);
        free(detector);
    }
}
