#include "DSPetNearSpeech.h"

#include <stdlib.h>
#include <string.h>

#include "DSPetAec.h"
#include "DSPetAecSync.h"
#include "DSPetSpeechDetector.h"
#include "esp_heap_caps.h"

enum { kPlaybackToMicUs = 2100, kFilterLeadUs = 1000 };

struct DSPetNearSpeech {
    DSPetAecSync         sync;
    DSPetSpeechCapture   capture;
    DSPetAec            *aec;
    DSPetSpeechDetector *detector;
    int16_t             *aecBuffers;
    size_t               aecSamples;
    int16_t              recording[kDSPetNearSpeechMaxRecordingSamples];
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
    DSPetNearSpeechReset(speech, true);
    *out = speech;
    return ESP_OK;
}

void DSPetNearSpeechReset(DSPetNearSpeech *speech, bool restartTimeline) {
    if (restartTimeline) {
        DSPetAecSyncInit(&speech->sync, kPlaybackToMicUs - kFilterLeadUs);
    }
    DSPetSpeechCaptureInit(&speech->capture, speech->recording, kDSPetNearSpeechMaxRecordingSamples);
    speech->pendingSamples = 0;
    speech->peakRms        = 0;
}

void DSPetNearSpeechRealign(DSPetNearSpeech *speech) {
    DSPetAecSyncInit(&speech->sync, kPlaybackToMicUs - kFilterLeadUs);
    speech->pendingSamples       = 0;
    speech->capture.index        = 0;
    speech->capture.filled       = 0;
    speech->capture.voicedFrames = 0;
    speech->capture.quietFrames  = 0;
    speech->capture.strongInRun  = false;
}

static void classify(
    DSPetNearSpeech *speech, const int16_t *pcm, size_t samples, DSPetNearSpeechMode mode, DSPetSpeechEvent *event) {
    const bool armed = mode != kDSPetNearSpeechDisarmed;
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
            DSPetSpeechEvidence evidence = DSPetSpeechDetectorProcess(
                speech->detector, speech->pending, speech->capture.active, mode == kDSPetNearSpeechPlaying, &rms);
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

esp_err_t DSPetNearSpeechPush(DSPetNearSpeech          *speech,
                              const DSPetAudioTapBlock *block,
                              DSPetNearSpeechMode       mode,
                              DSPetSpeechEvent         *event) {
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
    if (mode == kDSPetNearSpeechAlignOnly) {
        return ESP_OK;
    }
    esp_err_t result = DSPetAecProcess(speech->aec, mic, reference, output, speech->aecSamples);
    if (result == ESP_OK) {
        classify(speech, output, speech->aecSamples, mode, event);
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
