#include "DSPetLocalVoice.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>

#include "DSPetNearSpeech.h"
#include "DSPetPlayback.h"
#include "DSPetWakeWord.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

enum {
    kTapWaitMs       = 20,
    kListenTimeoutUs = 8000000,
    kWakeTailUs      = 220000,
    kReplyTailUs     = 220000,
    kBargeWarmupUs   = 850000,
    kReplyDelayUs    = 250000,
};

typedef struct {
    DSPetVoiceSession session;
    DSPetNearSpeech  *speech;
    DSPetWakeWord    *wakeWord;
    int64_t           armedUs;
    int64_t           deadlineUs;
    int64_t           cancelledUs;
    uint32_t          playbackTurnId;
} LocalVoice;

extern const int16_t kReplyPcmStart[] asm("_binary_local_voice_reply_pcm_start");
extern const int16_t kReplyPcmEnd[] asm("_binary_local_voice_reply_pcm_end");

static const char *const kTag = "local_voice";
static TaskHandle_t      sTask;
static atomic_int        sMode;
static atomic_int        sState;
static atomic_bool       sStartRequested;

const char *DSPetLocalVoiceStateName(DSPetVoiceState state) {
    switch (state) {
        case kDSPetVoiceIdle:      return "idle";
        case kDSPetVoiceListening: return "listening";
        case kDSPetVoiceWaiting:   return "waiting";
        case kDSPetVoiceSpeaking:  return "speaking";
    }
    return "unknown";
}

static void publish(const LocalVoice *voice, const char *event) {
    atomic_store(&sState, voice->session.state);
    printf("DSPET_VOICE event=%s state=%s turn=%" PRIu32 "\n",
           event,
           DSPetLocalVoiceStateName(voice->session.state),
           voice->session.turnId);
}

static void beginListening(LocalVoice *voice, int64_t nowUs, int64_t delayUs, bool fromIdle) {
    DSPetNearSpeechReset(voice->speech, fromIdle);
    voice->armedUs    = nowUs + delayUs;
    voice->deadlineUs = voice->armedUs + kListenTimeoutUs;
}

static void startSession(LocalVoice *voice, const char *source) {
    if (DSPetVoiceSessionApply(&voice->session, kDSPetVoiceStart, 0)) {
        beginListening(voice, esp_timer_get_time(), kWakeTailUs, true);
        publish(voice, source);
    }
}

static void handleSpeech(LocalVoice *voice, DSPetSpeechEvent event, int64_t nowUs) {
    if (event == kDSPetSpeechStarted) {
        const bool interrupting = voice->session.state == kDSPetVoiceSpeaking;
        if (DSPetVoiceSessionApply(&voice->session, kDSPetVoiceSpeechStart, 0)) {
            if (interrupting) {
                voice->cancelledUs = esp_timer_get_time();
                DSPetPlaybackCancel();
            }
            publish(voice, interrupting ? "barge_in" : "speech_start");
        }
    } else if (event == kDSPetSpeechFinished && DSPetVoiceSessionApply(&voice->session, kDSPetVoiceSpeechEnd, 0)) {
        size_t samples;
        size_t preRoll;
        DSPetNearSpeechRecording(voice->speech, &samples, &preRoll);
        printf("DSPET_VOICE_CAPTURE turn=%" PRIu32 " samples=%u preroll=%u capped=%u\n",
               voice->session.turnId,
               (unsigned) samples,
               (unsigned) preRoll,
               samples == kDSPetNearSpeechMaxRecordingSamples ? 1u : 0u);
        voice->deadlineUs = nowUs + kReplyDelayUs;
        publish(voice, "speech_end");
    }
}

static esp_err_t takePlaybackEvents(LocalVoice *voice) {
    DSPetPlaybackEvent event;
    while (DSPetPlaybackTakeEvent(&event)) {
        if (event.kind == kDSPetPlaybackEventQueued) {
            voice->armedUs = event.occurredUs + kBargeWarmupUs;
            publish(voice, "playback_started");
            continue;
        }
        const uint32_t played = voice->playbackTurnId;
        voice->playbackTurnId = 0;
        if (event.kind == kDSPetPlaybackEventFailed) {
            return event.error;
        }
        if (voice->cancelledUs) {
            printf("DSPET_VOICE_DRAIN turn=%" PRIu32 " cancel_to_drain_us=%" PRId64 "\n",
                   played,
                   event.occurredUs - voice->cancelledUs);
            voice->cancelledUs = 0;
        }
        if (event.kind == kDSPetPlaybackEventFinished &&
            DSPetVoiceSessionApply(&voice->session, kDSPetVoiceReplyEnd, played)) {
            beginListening(voice, event.occurredUs, kReplyTailUs, false);
            publish(voice, "playback_finished");
        }
    }
    return ESP_OK;
}

static esp_err_t advanceSession(LocalVoice *voice, int64_t nowUs) {
    if (voice->session.state == kDSPetVoiceWaiting && !voice->playbackTurnId && nowUs >= voice->deadlineUs) {
        // 本地回复使用固定中文；就绪和完成事件都携带发起播放时的回合编号。
        const uint32_t turn  = voice->session.turnId;
        esp_err_t      error = DSPetPlaybackStart(kReplyPcmStart, kReplyPcmEnd - kReplyPcmStart);
        if (error != ESP_OK) {
            return error;
        }
        voice->playbackTurnId = turn;
        voice->armedUs        = INT64_MAX;
        DSPetNearSpeechReset(voice->speech, false);
        DSPetVoiceSessionApply(&voice->session, kDSPetVoiceReplyReady, turn);
        publish(voice, "reply_ready");
    } else if (voice->session.state == kDSPetVoiceListening && nowUs >= voice->deadlineUs &&
               DSPetVoiceSessionApply(&voice->session, kDSPetVoiceListenTimeout, 0)) {
        DSPetWakeWordReset(voice->wakeWord);
        publish(voice, "listen_timeout");
    }
    return ESP_OK;
}

static esp_err_t processBlock(LocalVoice *voice, const DSPetAudioTapBlock *block) {
    if (voice->session.state == kDSPetVoiceIdle) {
        if (block->source == kDSPetAudioTapMic &&
            DSPetWakeWordPush(voice->wakeWord, block->samples, kDSPetAudioDeviceBlockSamples)) {
            startSession(voice, "wake_word");
        }
        return ESP_OK;
    }

    const int64_t       nowUs = esp_timer_get_time();
    DSPetNearSpeechMode mode  = kDSPetNearSpeechDisarmed;
    if (voice->session.recording || (nowUs >= voice->armedUs && voice->session.state != kDSPetVoiceWaiting)) {
        mode = voice->playbackTurnId ? kDSPetNearSpeechPlaying : kDSPetNearSpeechListening;
    }
    DSPetSpeechEvent event;
    esp_err_t        error = DSPetNearSpeechPush(voice->speech, block, mode, &event);
    if (error == ESP_OK) {
        handleSpeech(voice, event, nowUs);
    }
    return error;
}

static esp_err_t runSession(LocalVoice *voice) {
    if (atomic_exchange(&sStartRequested, false)) {
        startSession(voice, "manual_start");
    }
    esp_err_t error = takePlaybackEvents(voice);
    if (error == ESP_OK) {
        error = advanceSession(voice, esp_timer_get_time());
    }
    DSPetAudioTapBlock block;
    if (error == ESP_OK && DSPetAudioDeviceTakeTap(&block, kTapWaitMs)) {
        error = processBlock(voice, &block);
    }
    return error;
}

static void runVoice(LocalVoice *voice) {
    // 模型初始化比 DMA 队列的时间窗长，准备工作都在打开采样前完成。
    esp_err_t error = DSPetWakeWordCreate(&voice->wakeWord);
    if (error == ESP_OK) {
        error = DSPetNearSpeechCreate(&voice->speech);
    }
    if (error == ESP_OK) {
        error = DSPetAudioDeviceOpen();
    }
    const bool deviceOpen = error == ESP_OK;
    if (deviceOpen) {
        error = DSPetAudioDeviceStartTap();
    }
    if (error == ESP_OK) {
        atomic_store(&sMode, kDSPetLocalVoiceOn);
        publish(voice, "ready");
        while (atomic_load(&sMode) == kDSPetLocalVoiceOn && error == ESP_OK) {
            error = runSession(voice);
        }
    }
    atomic_store(&sMode, kDSPetLocalVoiceStopping);
    atomic_store(&sStartRequested, false);
    DSPetVoiceSessionApply(&voice->session, kDSPetVoiceStop, 0);
    if (voice->playbackTurnId) {
        DSPetPlaybackCancel();
        // 等播放终态后再关闭 DMA；播放服务内部的写入和排空都有上限。
        while (voice->playbackTurnId) {
            takePlaybackEvents(voice);
            DSPetAudioTapBlock block;
            DSPetAudioDeviceTakeTap(&block, kTapWaitMs);
        }
    }
    if (deviceOpen) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(DSPetAudioDeviceClose());
    }
    DSPetNearSpeechDestroy(voice->speech);
    DSPetWakeWordDestroy(voice->wakeWord);
    voice->speech      = NULL;
    voice->wakeWord    = NULL;
    voice->cancelledUs = 0;
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Voice session stopped: %s", esp_err_to_name(error));
    }
    atomic_store(&sMode, kDSPetLocalVoiceOff);
    publish(voice, "disabled");
}

static void voiceTask(void *context) {
    LocalVoice voice = {0};
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (atomic_load(&sMode) == kDSPetLocalVoiceStarting) {
            runVoice(&voice);
        }
    }
}

esp_err_t DSPetLocalVoiceInit(void) {
    const esp_err_t error = DSPetPlaybackInit();
    if (error != ESP_OK) {
        return error;
    }
    const BaseType_t core = CONFIG_FREERTOS_NUMBER_OF_CORES > 1 ? 1 : 0;
    if (xTaskCreatePinnedToCore(voiceTask, "local-voice", 6144, NULL, 3, &sTask, core) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return DSPetLocalVoiceSetEnabled(true);
}

esp_err_t DSPetLocalVoiceSetEnabled(bool enabled) {
    if (!sTask) {
        return ESP_ERR_INVALID_STATE;
    }
    int expected = enabled ? kDSPetLocalVoiceOff : kDSPetLocalVoiceOn;
    if (atomic_compare_exchange_strong(
            &sMode, &expected, enabled ? kDSPetLocalVoiceStarting : kDSPetLocalVoiceStopping)) {
        if (enabled) {
            xTaskNotifyGive(sTask);
        }
        return ESP_OK;
    }
    return (enabled ? expected == kDSPetLocalVoiceOn : expected == kDSPetLocalVoiceOff) ? ESP_OK
                                                                                        : ESP_ERR_INVALID_STATE;
}

esp_err_t DSPetLocalVoiceStart(void) {
    if (DSPetLocalVoiceGetMode() != kDSPetLocalVoiceOn || DSPetLocalVoiceGetState() != kDSPetVoiceIdle) {
        return ESP_ERR_INVALID_STATE;
    }
    atomic_store(&sStartRequested, true);
    return ESP_OK;
}

DSPetLocalVoiceMode DSPetLocalVoiceGetMode(void) { return atomic_load(&sMode); }

DSPetVoiceState DSPetLocalVoiceGetState(void) { return atomic_load(&sState); }
