#include "DSPetNearSpeech.h"

#include <stdlib.h>
#include <string.h>

#include "DSPetAec.h"
#include "DSPetAecSync.h"
#include "DSPetSpeechDetector.h"
#include "esp_heap_caps.h"

enum { kPlaybackToMicUs = 2100, kFilterLeadUs = 1000, kRecordSamples = 4 * kDSPetAudioDeviceSampleRateHz };

struct DSPetNearSpeech {
    DSPetAecSync         sync;
    DSPetSpeechCapture   capture;
    DSPetAec            *aec;
    DSPetSpeechDetector *detector;
    int16_t             *aecBuffers;
    size_t               aecSamples;
    int16_t              recording[kRecordSamples];
    int16_t              pending[kDSPetSpeechFrameSamples];
    size_t               pendingSamples;
    float                peakRms;
};

esp_err_t DSPetNearSpeechCreate(DSPetNearSpeech **out) {
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out                    = NULL;
    DSPetNearSpeech *speech = heap_caps_calloc(1, sizeof(*speech), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!speech) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t result = DSPetAecCreate(&speech->aec);
    if (result == ESP_OK) {
        result = DSPetSpeechDetectorCreate(&speech->detector);
    }
    if (result == ESP_OK) {
        speech->aecSamples = DSPetAecFrameSamples(speech->aec);
        speech->aecBuffers =
            heap_caps_malloc(3 * speech->aecSamples * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!speech->aecBuffers) {
            result = ESP_ERR_NO_MEM;
        }
    }
    if (result != ESP_OK) {
        DSPetNearSpeechDestroy(speech);
        return result;
    }
    DSPetAecSyncInit(&speech->sync, kPlaybackToMicUs - kFilterLeadUs);
    DSPetSpeechCaptureInit(&speech->capture, speech->recording, kRecordSamples);
    *out = speech;
    return ESP_OK;
}

static void classify(DSPetNearSpeech *speech, const int16_t *pcm, size_t samples, bool armed, DSPetSpeechEvent *event) {
    while (samples && !speech->capture.done) {
        size_t count = kDSPetSpeechFrameSamples - speech->pendingSamples;
        if (count > samples) {
            count = samples;
        }
        memcpy(speech->pending + speech->pendingSamples, pcm, count * sizeof(*pcm));
        speech->pendingSamples += count;
        pcm += count;
        samples -= count;
        if (speech->pendingSamples == kDSPetSpeechFrameSamples) {
            float               rms;
            DSPetSpeechEvidence evidence =
                DSPetSpeechDetectorProcess(speech->detector, speech->pending, speech->capture.active, &rms);
            if (armed && rms > speech->peakRms) {
                speech->peakRms = rms;
            }
            DSPetSpeechEvent next = DSPetSpeechCapturePush(&speech->capture, speech->pending, evidence, armed);
            if (next != kDSPetSpeechNoEvent) {
                *event = next;
            }
            speech->pendingSamples = 0;
        }
    }
}

esp_err_t
DSPetNearSpeechPush(DSPetNearSpeech *speech, const DSPetAudioTapBlock *block, bool armed, DSPetSpeechEvent *event) {
    if (!speech || !block || !event) {
        return ESP_ERR_INVALID_ARG;
    }
    *event                    = kDSPetSpeechNoEvent;
    DSPetAecSyncSource source = block->source == kDSPetAudioTapMic ? kDSPetAecSyncMic : kDSPetAecSyncReference;
    if (!DSPetAecSyncPush(
            &speech->sync, source, block->samples, kDSPetAudioDeviceBlockSamples, block->blockIndex, block->dmaEofUs)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    int16_t           *mic       = speech->aecBuffers;
    int16_t           *reference = mic + speech->aecSamples;
    int16_t           *output    = reference + speech->aecSamples;
    DSPetAecSyncResult sync      = DSPetAecSyncTake(&speech->sync, mic, reference, speech->aecSamples);
    if (sync == kDSPetAecSyncWaiting) {
        return ESP_OK;
    }
    if (sync != kDSPetAecSyncReady) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    esp_err_t result = DSPetAecProcess(speech->aec, mic, reference, output, speech->aecSamples);
    if (result == ESP_OK) {
        classify(speech, output, speech->aecSamples, armed, event);
    }
    return result;
}

int16_t *DSPetNearSpeechRecording(DSPetNearSpeech *speech, size_t *sampleCount, size_t *preRollSamples) {
    if (!speech->capture.done) {
        return NULL;
    }
    *sampleCount    = speech->capture.recordedSamples;
    *preRollSamples = speech->capture.preRollSamples;
    return speech->recording;
}

float DSPetNearSpeechPeakRms(const DSPetNearSpeech *speech) { return speech->peakRms; }

void DSPetNearSpeechDestroy(DSPetNearSpeech *speech) {
    if (speech) {
        DSPetAecDestroy(speech->aec);
        DSPetSpeechDetectorDestroy(speech->detector);
        free(speech->aecBuffers);
        free(speech);
    }
}
