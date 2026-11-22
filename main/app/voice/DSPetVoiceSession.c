#include "DSPetVoiceSession.h"

static void listen(DSPetVoiceSession *session, bool recording) {
    ++session->turnId;
    if (!session->turnId) {
        ++session->turnId;
    }
    session->state     = kDSPetVoiceListening;
    session->recording = recording;
}

bool DSPetVoiceSessionApply(DSPetVoiceSession *session, DSPetVoiceEvent event, uint32_t replyTurnId) {
    switch (event) {
        case kDSPetVoiceStart:
            if (session->state == kDSPetVoiceIdle) {
                listen(session, false);
                return true;
            }
            break;
        case kDSPetVoiceSpeechStart:
            if (session->state == kDSPetVoiceSpeaking) {
                listen(session, true);
                return true;
            }
            if (session->state == kDSPetVoiceListening && !session->recording) {
                session->recording = true;
                return true;
            }
            break;
        case kDSPetVoiceSpeechEnd:
            if (session->state == kDSPetVoiceListening && session->recording) {
                session->recording = false;
                session->state     = kDSPetVoiceWaiting;
                return true;
            }
            break;
        case kDSPetVoiceReplyReady:
            if (session->state == kDSPetVoiceWaiting && replyTurnId == session->turnId) {
                session->state = kDSPetVoiceSpeaking;
                return true;
            }
            break;
        case kDSPetVoiceReplyEnd:
            if (session->state == kDSPetVoiceSpeaking && replyTurnId == session->turnId) {
                listen(session, false);
                return true;
            }
            break;
        case kDSPetVoiceListenTimeout:
            if (session->state != kDSPetVoiceListening || session->recording) {
                break;
            }
            // 未开口超时和手动停止都回到待机。
            session->state = kDSPetVoiceIdle;
            return true;
        case kDSPetVoiceStop:
            session->recording = false;
            session->state     = kDSPetVoiceIdle;
            return true;
    }
    return false;
}
