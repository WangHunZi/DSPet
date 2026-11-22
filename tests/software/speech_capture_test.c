#include <assert.h>

#include "DSPetSpeechCapture.h"

static DSPetSpeechEvent push(DSPetSpeechCapture *capture, int16_t value, DSPetSpeechEvidence evidence, bool armed) {
    int16_t pcm[kDSPetSpeechFrameSamples];
    for (size_t i = 0; i < kDSPetSpeechFrameSamples; ++i) {
        pcm[i] = value;
    }
    return DSPetSpeechCapturePush(capture, pcm, evidence, armed);
}

int main(void) {
    int16_t            recording[2 * kDSPetSpeechPreRollSamples];
    DSPetSpeechCapture capture;
    DSPetSpeechCaptureInit(&capture, recording, sizeof(recording) / sizeof(recording[0]));
    for (int i = 1; i <= 25; ++i) {
        // 未武装时即使判到人声，也只保留短历史。
        assert(push(&capture, (int16_t) i, kDSPetSpeechStrong, false) == kDSPetSpeechNoEvent);
    }
    // 键盘类的单帧瞬态不能触发。
    assert(push(&capture, 26, kDSPetSpeechStrong, true) == kDSPetSpeechNoEvent);
    assert(push(&capture, 27, kDSPetSpeechSilent, true) == kDSPetSpeechNoEvent);
    for (int i = 28; i < 31; ++i) {
        assert(push(&capture, (int16_t) i, kDSPetSpeechVoiced, true) == kDSPetSpeechNoEvent);
    }
    assert(push(&capture, 31, kDSPetSpeechVoiced, true) == kDSPetSpeechNoEvent);
    assert(push(&capture, 32, kDSPetSpeechStrong, true) == kDSPetSpeechStarted);
    assert(capture.preRollSamples == kDSPetSpeechPreRollSamples);
    assert(recording[0] == 17);
    assert(recording[kDSPetSpeechPreRollSamples - 1] == 32);
    assert(push(&capture, 33, kDSPetSpeechVoiced, true) == kDSPetSpeechNoEvent);
    assert(recording[kDSPetSpeechPreRollSamples] == 33);

    int16_t shortRecording[70 * kDSPetSpeechFrameSamples];
    DSPetSpeechCaptureInit(&capture, shortRecording, sizeof(shortRecording) / sizeof(shortRecording[0]));
    for (int i = 0; i < 3; ++i) {
        assert(push(&capture, 1, kDSPetSpeechVoiced, true) == kDSPetSpeechNoEvent);
    }
    assert(push(&capture, 1, kDSPetSpeechStrong, true) == kDSPetSpeechStarted);
    for (int i = 0; i < 25; ++i) {
        assert(push(&capture, 0, kDSPetSpeechSilent, true) == kDSPetSpeechNoEvent);
    }
    assert(push(&capture, 2, kDSPetSpeechVoiced, false) == kDSPetSpeechNoEvent);
    for (int i = 0; i < 34; ++i) {
        assert(push(&capture, 0, kDSPetSpeechSilent, true) == kDSPetSpeechNoEvent);
    }
    assert(push(&capture, 0, kDSPetSpeechSilent, true) == kDSPetSpeechFinished);
    assert(capture.recordedSamples == 50 * kDSPetSpeechFrameSamples);
    assert(shortRecording[29 * kDSPetSpeechFrameSamples] == 2);
    return 0;
}
