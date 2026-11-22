#include <assert.h>

#include "DSPetVoiceSession.h"

static void testAlwaysListening(void) {
    DSPetVoiceSession session = {.alwaysListening = true};
    assert(DSPetVoiceSessionApply(&session, kDSPetVoiceStart, 0));
    assert(!DSPetVoiceSessionApply(&session, kDSPetVoiceListenTimeout, 0));
    assert(DSPetVoiceSessionApply(&session, kDSPetVoiceSpeechStart, 0));
    assert(DSPetVoiceSessionApply(&session, kDSPetVoiceSpeechEnd, 0));
    const uint32_t ignored = session.turnId;
    assert(!DSPetVoiceSessionApply(&session, kDSPetVoiceReplyIgnored, ignored - 1));
    assert(DSPetVoiceSessionApply(&session, kDSPetVoiceReplyIgnored, ignored));
    assert(!DSPetVoiceSessionApply(&session, kDSPetVoiceReplyReady, ignored));
    assert(!DSPetVoiceSessionApply(&session, kDSPetVoiceListenTimeout, 0));
    assert(DSPetVoiceSessionApply(&session, kDSPetVoiceSpeechStart, 0));
    assert(DSPetVoiceSessionApply(&session, kDSPetVoiceSpeechEnd, 0));
    assert(DSPetVoiceSessionApply(&session, kDSPetVoiceReplyReady, session.turnId));
    assert(!DSPetVoiceSessionApply(&session, kDSPetVoiceReplyIgnored, session.turnId));
    assert(DSPetVoiceSessionApply(&session, kDSPetVoiceReplyEnd, session.turnId));
    assert(!DSPetVoiceSessionApply(&session, kDSPetVoiceListenTimeout, 0));
    assert(DSPetVoiceSessionApply(&session, kDSPetVoiceStop, 0));
    assert(session.state == kDSPetVoiceIdle);
}

int main(void) {
    testAlwaysListening();
    DSPetVoiceSession session = {0};
    assert(DSPetVoiceSessionApply(&session, kDSPetVoiceStart, 0));
    assert(session.state == kDSPetVoiceListening);
    const uint32_t captured = session.turnId;
    assert(!DSPetVoiceSessionApply(&session, kDSPetVoiceSpeechEnd, 0));
    assert(DSPetVoiceSessionApply(&session, kDSPetVoiceSpeechStart, 0));
    assert(session.state == kDSPetVoiceRecording && session.turnId == captured);
    assert(!DSPetVoiceSessionApply(&session, kDSPetVoiceSpeechStart, 0));
    assert(!DSPetVoiceSessionApply(&session, kDSPetVoiceListenTimeout, 0));
    assert(DSPetVoiceSessionApply(&session, kDSPetVoiceSpeechEnd, 0));
    assert(session.state == kDSPetVoiceWaiting);
    uint32_t first = session.turnId;
    assert(DSPetVoiceSessionApply(&session, kDSPetVoiceReplyReady, first));
    assert(session.state == kDSPetVoiceSpeaking);
    assert(DSPetVoiceSessionApply(&session, kDSPetVoiceReplyEnd, first));

    assert(session.state == kDSPetVoiceListening && session.turnId != first);
    assert(DSPetVoiceSessionApply(&session, kDSPetVoiceSpeechStart, 0));
    assert(DSPetVoiceSessionApply(&session, kDSPetVoiceSpeechEnd, 0));
    assert(!DSPetVoiceSessionApply(&session, kDSPetVoiceReplyReady, first));
    uint32_t second = session.turnId;
    assert(DSPetVoiceSessionApply(&session, kDSPetVoiceReplyReady, second));

    assert(DSPetVoiceSessionApply(&session, kDSPetVoiceSpeechStart, 0));
    assert(session.state == kDSPetVoiceRecording && session.turnId != second);
    assert(!DSPetVoiceSessionApply(&session, kDSPetVoiceReplyEnd, second));
    assert(DSPetVoiceSessionApply(&session, kDSPetVoiceSpeechEnd, 0));
    assert(!DSPetVoiceSessionApply(&session, kDSPetVoiceReplyReady, second));
    uint32_t third = session.turnId;
    assert(DSPetVoiceSessionApply(&session, kDSPetVoiceReplyReady, third));
    assert(!DSPetVoiceSessionApply(&session, kDSPetVoiceReplyEnd, second));
    assert(DSPetVoiceSessionApply(&session, kDSPetVoiceReplyEnd, third));
    assert(DSPetVoiceSessionApply(&session, kDSPetVoiceListenTimeout, 0));
    assert(session.state == kDSPetVoiceIdle);

    assert(DSPetVoiceSessionApply(&session, kDSPetVoiceStart, 0));
    assert(session.turnId != third);
    assert(DSPetVoiceSessionApply(&session, kDSPetVoiceSpeechStart, 0));
    assert(DSPetVoiceSessionApply(&session, kDSPetVoiceSpeechEnd, 0));
    assert(!DSPetVoiceSessionApply(&session, kDSPetVoiceReplyFailed, third));
    uint32_t failed = session.turnId;
    assert(DSPetVoiceSessionApply(&session, kDSPetVoiceReplyFailed, failed));
    assert(session.state == kDSPetVoiceListening && session.turnId != failed);
    assert(DSPetVoiceSessionApply(&session, kDSPetVoiceStop, 0));
    assert(session.state == kDSPetVoiceIdle);
    return 0;
}
