#include <assert.h>

#include "DSPetVoiceSession.h"

int main(void) {
    DSPetVoiceSession session = {0};
    assert(DSPetVoiceSessionApply(&session, kDSPetVoiceStart, 0));
    assert(session.state == kDSPetVoiceListening);
    assert(!DSPetVoiceSessionApply(&session, kDSPetVoiceSpeechEnd, 0));
    assert(DSPetVoiceSessionApply(&session, kDSPetVoiceSpeechStart, 0));
    assert(!DSPetVoiceSessionApply(&session, kDSPetVoiceListenTimeout, 0));
    assert(DSPetVoiceSessionApply(&session, kDSPetVoiceSpeechEnd, 0));
    assert(session.state == kDSPetVoiceWaiting);
    uint32_t first = session.turnId;
    assert(DSPetVoiceSessionApply(&session, kDSPetVoiceReplyReady, first));
    assert(session.state == kDSPetVoiceSpeaking);
    assert(DSPetVoiceSessionApply(&session, kDSPetVoiceReplyEnd, first));

    assert(session.state == kDSPetVoiceListening && !session.recording && session.turnId != first);
    assert(DSPetVoiceSessionApply(&session, kDSPetVoiceSpeechStart, 0));
    assert(DSPetVoiceSessionApply(&session, kDSPetVoiceSpeechEnd, 0));
    assert(!DSPetVoiceSessionApply(&session, kDSPetVoiceReplyReady, first));
    uint32_t second = session.turnId;
    assert(DSPetVoiceSessionApply(&session, kDSPetVoiceReplyReady, second));

    assert(DSPetVoiceSessionApply(&session, kDSPetVoiceSpeechStart, 0));
    assert(session.state == kDSPetVoiceListening && session.recording && session.turnId != second);
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
    assert(DSPetVoiceSessionApply(&session, kDSPetVoiceStop, 0));
    assert(session.state == kDSPetVoiceIdle);
    return 0;
}
