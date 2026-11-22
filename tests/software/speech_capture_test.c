#include <assert.h>

#include "DSPetSpeechCapture.h"

static DSPetSpeechEvent push(DSPetSpeechCapture *capture, int16_t value, bool voiced, bool armed) {
    int16_t pcm[kDSPetSpeechFrameSamples];
    for (size_t i = 0; i < kDSPetSpeechFrameSamples; ++i) {
        pcm[i] = value;
    }
    return DSPetSpeechCapturePush(capture, pcm, voiced, armed);
}

int main(void) {
    int16_t            recording[2 * kDSPetSpeechPreRollSamples];
    DSPetSpeechCapture capture;
    DSPetSpeechCaptureInit(&capture, recording, sizeof(recording) / sizeof(recording[0]));
    for (int i = 1; i <= 25; ++i) {
        // 未武装时即使判到人声，也只保留短历史。
        assert(push(&capture, (int16_t) i, true, false) == kDSPetSpeechNoEvent);
    }
    // 键盘类的单帧瞬态不能触发。
    assert(push(&capture, 26, true, true) == kDSPetSpeechNoEvent);
    assert(push(&capture, 27, false, true) == kDSPetSpeechNoEvent);
    for (int i = 28; i < 31; ++i) {
        assert(push(&capture, (int16_t) i, true, true) == kDSPetSpeechNoEvent);
    }
    assert(push(&capture, 31, true, true) == kDSPetSpeechStarted);
    assert(capture.preRollSamples == kDSPetSpeechPreRollSamples);
    assert(recording[0] == 16);
    assert(recording[kDSPetSpeechPreRollSamples - 1] == 31);
    assert(push(&capture, 32, true, true) == kDSPetSpeechNoEvent);
    assert(recording[kDSPetSpeechPreRollSamples] == 32);

    int16_t shortRecording[40 * kDSPetSpeechFrameSamples];
    DSPetSpeechCaptureInit(&capture, shortRecording, sizeof(shortRecording) / sizeof(shortRecording[0]));
    for (int i = 0; i < 3; ++i) {
        assert(push(&capture, 1, true, true) == kDSPetSpeechNoEvent);
    }
    assert(push(&capture, 1, true, true) == kDSPetSpeechStarted);
    for (int i = 0; i < 21; ++i) {
        assert(push(&capture, 0, false, true) == kDSPetSpeechNoEvent);
    }
    assert(push(&capture, 0, false, true) == kDSPetSpeechFinished);
    assert(capture.recordedSamples == 10 * kDSPetSpeechFrameSamples);
    return 0;
}
