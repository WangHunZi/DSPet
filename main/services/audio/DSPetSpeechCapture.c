#include "DSPetSpeechCapture.h"

#include <string.h>

enum { kSpeechFrames = 4, kEndpointFrames = 22, kTailFrames = 6 };

void DSPetSpeechCaptureInit(DSPetSpeechCapture *capture, int16_t *recording, size_t capacitySamples) {
    memset(capture, 0, sizeof(*capture));
    capture->recording       = recording;
    capture->capacitySamples = capacitySamples;
}

static void saveHistory(DSPetSpeechCapture *capture, const int16_t *pcm) {
    memcpy(capture->history + capture->index, pcm, kDSPetSpeechFrameSamples * sizeof(*pcm));
    capture->index = (capture->index + kDSPetSpeechFrameSamples) % kDSPetSpeechPreRollSamples;
    if (capture->filled < kDSPetSpeechPreRollSamples) {
        capture->filled += kDSPetSpeechFrameSamples;
    }
}

static void copyHistory(DSPetSpeechCapture *capture) {
    const size_t start = capture->filled == kDSPetSpeechPreRollSamples ? capture->index : 0;
    const size_t first = capture->filled - start;
    memcpy(capture->recording, capture->history + start, first * sizeof(int16_t));
    if (start) {
        memcpy(capture->recording + first, capture->history, start * sizeof(int16_t));
    }
    capture->preRollSamples  = capture->filled;
    capture->recordedSamples = capture->filled;
}

DSPetSpeechEvent DSPetSpeechCapturePush(DSPetSpeechCapture *capture, const int16_t *pcm, bool voiced, bool armed) {
    if (capture->done) {
        return kDSPetSpeechNoEvent;
    }
    if (!capture->active) {
        saveHistory(capture, pcm);
        capture->voicedFrames = armed && voiced ? capture->voicedFrames + 1 : 0;
        if (capture->voicedFrames < kSpeechFrames) {
            return kDSPetSpeechNoEvent;
        }
        copyHistory(capture);
        capture->active = true;
        return kDSPetSpeechStarted;
    }

    size_t remaining = capture->capacitySamples - capture->recordedSamples;
    size_t count     = remaining < kDSPetSpeechFrameSamples ? remaining : kDSPetSpeechFrameSamples;
    memcpy(capture->recording + capture->recordedSamples, pcm, count * sizeof(*pcm));
    capture->recordedSamples += count;
    capture->quietFrames = voiced ? 0 : capture->quietFrames + 1;
    if (capture->quietFrames == kEndpointFrames) {
        capture->recordedSamples -= (kEndpointFrames - kTailFrames - 1) * kDSPetSpeechFrameSamples + count;
    } else if (capture->recordedSamples < capture->capacitySamples) {
        return kDSPetSpeechNoEvent;
    }
    capture->done = true;
    return kDSPetSpeechFinished;
}
