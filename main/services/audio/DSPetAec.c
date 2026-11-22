#include "DSPetAec.h"

#include <stdlib.h>
#include <string.h>

#include "esp_aec.h"
#include "esp_heap_caps.h"

enum { kSampleRateHz = 16000, kAlignmentBytes = 16 };

struct DSPetAec {
    aec_handle_t *engine;
    size_t        frameSamples;
    size_t        strideSamples;
    int16_t      *buffer;
};

esp_err_t DSPetAecCreate(DSPetAec **out) {
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out          = NULL;
    DSPetAec *aec = calloc(1, sizeof(*aec));
    if (!aec) {
        return ESP_ERR_NO_MEM;
    }
    aec_config_t config = {
        .mic_num       = 1,
        .ref_num       = 1,
        .out_num       = 1,
        .filter_length = 4,
        .sample_rate   = kSampleRateHz,
        .caps          = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
        .mode          = AEC_MODE_FD_LOW_COST,
        .nlp_level     = AEC_NLP_LEVEL_NORMAL,
    };
    aec->engine = aec_create_from_config(&config);
    if (!aec->engine) {
        DSPetAecDestroy(aec);
        return ESP_ERR_NO_MEM;
    }
    aec->frameSamples                = (size_t) aec_get_chunksize(aec->engine);
    const size_t samplesPerAlignment = kAlignmentBytes / sizeof(int16_t);
    aec->strideSamples = (aec->frameSamples + samplesPerAlignment - 1) / samplesPerAlignment * samplesPerAlignment;
    aec->buffer        = heap_caps_aligned_alloc(
        kAlignmentBytes, aec->strideSamples * sizeof(int16_t) * 3, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!aec->buffer) {
        DSPetAecDestroy(aec);
        return ESP_ERR_NO_MEM;
    }
    *out = aec;
    return ESP_OK;
}

size_t DSPetAecFrameSamples(const DSPetAec *aec) { return aec->frameSamples; }

esp_err_t
DSPetAecProcess(DSPetAec *aec, const int16_t *mic, const int16_t *reference, int16_t *output, size_t sampleCount) {
    if (!aec || !mic || !reference || !output || sampleCount != aec->frameSamples) {
        return ESP_ERR_INVALID_ARG;
    }
    int16_t     *micAligned    = aec->buffer;
    int16_t     *refAligned    = micAligned + aec->strideSamples;
    int16_t     *outputAligned = refAligned + aec->strideSamples;
    const size_t bytes         = sampleCount * sizeof(int16_t);
    memcpy(micAligned, mic, bytes);
    memcpy(refAligned, reference, bytes);
    aec_process(aec->engine, micAligned, refAligned, outputAligned);
    memcpy(output, outputAligned, bytes);
    return ESP_OK;
}

void DSPetAecDestroy(DSPetAec *aec) {
    if (aec) {
        if (aec->engine) {
            aec_destroy(aec->engine);
        }
        free(aec->buffer);
        free(aec);
    }
}
