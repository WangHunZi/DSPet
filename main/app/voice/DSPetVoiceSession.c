#include "DSPetVoiceSession.h"

static void beginTurn(DSPetVoiceSession *session, DSPetVoiceState state) {
    ++session->turnId;
    if (!session->turnId) {
        ++session->turnId;
    }
    session->state = state;
}

bool DSPetVoiceSessionApply(DSPetVoiceSession *session, DSPetVoiceEvent event, uint32_t replyTurnId) {
    switch (event) {
        case kDSPetVoiceStart:
            if (session->state == kDSPetVoiceIdle) {
                beginTurn(session, kDSPetVoiceListening);
                return true;
            }
            break;
        case kDSPetVoiceSpeechStart:
            if (session->state == kDSPetVoiceSpeaking) {
                beginTurn(session, kDSPetVoiceRecording);
                return true;
            }
            if (session->state == kDSPetVoiceListening) {
                session->state = kDSPetVoiceRecording;
                return true;
            }
            break;
        case kDSPetVoiceSpeechEnd:
            if (session->state == kDSPetVoiceRecording) {
                session->state = kDSPetVoiceWaiting;
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
                beginTurn(session, kDSPetVoiceListening);
                return true;
            }
            break;
        case kDSPetVoiceReplyIgnored:
            if (session->state == kDSPetVoiceWaiting && replyTurnId == session->turnId) {
                beginTurn(session, kDSPetVoiceListening);
                return true;
            }
            break;
        case kDSPetVoiceReplyFailed:
            if ((session->state == kDSPetVoiceWaiting || session->state == kDSPetVoiceSpeaking) &&
                replyTurnId == session->turnId) {
                beginTurn(session, kDSPetVoiceListening);
                return true;
            }
            break;
        case kDSPetVoiceListenTimeout:
            if (session->state != kDSPetVoiceListening || session->alwaysListening) {
                break;
            }
            // 未开口超时和手动停止都回到待机。
            session->state = kDSPetVoiceIdle;
            return true;
        case kDSPetVoiceStop: session->state = kDSPetVoiceIdle; return true;
    }
    return false;
}
