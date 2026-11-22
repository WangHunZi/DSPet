#include "DSPetWakeWord.h"

#include <stdlib.h>

#include "DSPetAudioDevice.h"
#include "esp_wn_models.h"
#include "model_path.h"

enum { kMicGain = 16, kRearmSamples = kDSPetAudioDeviceSampleRateHz };

struct DSPetWakeWord {
    srmodel_list_t       *models;
    const esp_wn_iface_t *iface;
    model_iface_data_t   *model;
    int16_t              *frame;
    size_t                frameSamples;
    size_t                usedSamples;
    size_t                rearmSamples;
};

esp_err_t DSPetWakeWordCreate(DSPetWakeWord **out) {
    *out                    = NULL;
    DSPetWakeWord *wakeWord = calloc(1, sizeof(*wakeWord));
    if (!wakeWord) {
        return ESP_ERR_NO_MEM;
    }

    wakeWord->models = esp_srmodel_init("model");
    const char *name = wakeWord->models ? esp_srmodel_filter(wakeWord->models, "wn9_", "nihaoxiaozhi_tts") : NULL;
    wakeWord->iface  = name ? esp_wn_handle_from_name(name) : NULL;
    if (!wakeWord->iface) {
        DSPetWakeWordDestroy(wakeWord);
        return ESP_ERR_NOT_FOUND;
    }

    wakeWord->model = wakeWord->iface->create(name, DET_MODE_90);
    if (!wakeWord->model) {
        DSPetWakeWordDestroy(wakeWord);
        return ESP_ERR_NO_MEM;
    }
    wakeWord->frameSamples = (size_t) wakeWord->iface->get_samp_chunksize(wakeWord->model);
    wakeWord->frame        = malloc(wakeWord->frameSamples * sizeof(*wakeWord->frame));
    if (!wakeWord->frame) {
        DSPetWakeWordDestroy(wakeWord);
        return ESP_ERR_NO_MEM;
    }
    *out = wakeWord;
    return ESP_OK;
}

bool DSPetWakeWordPush(DSPetWakeWord *wakeWord, const int16_t *samples, size_t sampleCount) {
    for (size_t i = 0; i < sampleCount; ++i) {
        int32_t sample = (int32_t) samples[i] * kMicGain;
        if (sample < INT16_MIN) {
            sample = INT16_MIN;
        } else if (sample > INT16_MAX) {
            sample = INT16_MAX;
        }
        wakeWord->frame[wakeWord->usedSamples++] = (int16_t) sample;
        if (wakeWord->usedSamples == wakeWord->frameSamples) {
            wakeWord->usedSamples          = 0;
            const wakenet_state_t detected = wakeWord->iface->detect(wakeWord->model, wakeWord->frame);
            if (wakeWord->rearmSamples) {
                const size_t used =
                    wakeWord->frameSamples < wakeWord->rearmSamples ? wakeWord->frameSamples : wakeWord->rearmSamples;
                wakeWord->rearmSamples -= used;
            } else if (detected == WAKENET_DETECTED) {
                return true;
            }
        }
    }
    return false;
}

void DSPetWakeWordReset(DSPetWakeWord *wakeWord) {
    // ESP-SR 2.6.1 的 WakeNet9 clean 在本模型上访问空队列。检测持续推进窗口，过渡期忽略旧命中。
    wakeWord->usedSamples  = 0;
    wakeWord->rearmSamples = kRearmSamples;
}

void DSPetWakeWordDestroy(DSPetWakeWord *wakeWord) {
    if (wakeWord) {
        if (wakeWord->model) {
            wakeWord->iface->destroy(wakeWord->model);
        }
        if (wakeWord->models) {
            esp_srmodel_deinit(wakeWord->models);
        }
        free(wakeWord->frame);
        free(wakeWord);
    }
}
