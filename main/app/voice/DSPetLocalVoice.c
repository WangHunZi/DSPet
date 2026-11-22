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
#ifdef CONFIG_DSPET_DIRECT_TEST
#include "mbedtls/platform_util.h"
#endif

enum {
    kTapWaitMs           = 20,
    kListenTimeoutUs     = 8000000,
    kWakeTailUs          = 220000,
    kReplyTailUs         = 220000,
    kBargeWarmupUs       = 850000,
    kDirectBargeWarmupUs = 1250000,
    kReplyDelayUs        = 250000,
};

typedef struct {
    DSPetVoiceSession session;
    DSPetNearSpeech  *speech;
    DSPetWakeWord    *wakeWord;
    int64_t           armedUs;
    int64_t           deadlineUs;
    int64_t           cancelledUs;
    uint32_t          playbackTurnId;
#ifdef CONFIG_DSPET_DIRECT_TEST
    DSPetDirectVoice *direct;
    uint32_t          submittedTurnId;
    bool              newSession;
    bool              fixture;
#endif
} LocalVoice;

extern const int16_t kReplyPcmStart[] asm("_binary_local_voice_reply_pcm_start");
extern const int16_t kReplyPcmEnd[] asm("_binary_local_voice_reply_pcm_end");

static const char *const kTag = "local_voice";
static TaskHandle_t      sTask;
static atomic_int        sMode;
static atomic_int        sState;
static atomic_bool       sStartRequested;
#ifdef CONFIG_DSPET_DIRECT_TEST
static atomic_bool         sUseDirect;
static atomic_bool         sFixtureRequested;
static DSPetDirectSettings sDirectSettings;
#endif

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
#ifdef CONFIG_DSPET_DIRECT_TEST
        voice->newSession = true;
        voice->fixture    = false;
#endif
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
#ifdef CONFIG_DSPET_DIRECT_TEST
                if (voice->direct) {
                    DSPetDirectVoiceCancel(voice->direct, voice->playbackTurnId);
                }
#endif
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

#ifdef CONFIG_DSPET_DIRECT_TEST
static void directFailed(LocalVoice *voice, uint32_t turn, esp_err_t error) {
    if (DSPetVoiceSessionApply(&voice->session, kDSPetVoiceReplyFailed, turn)) {
        ESP_LOGW(kTag, "Direct turn %" PRIu32 " failed: %s", turn, esp_err_to_name(error));
        beginListening(voice, esp_timer_get_time(), kReplyTailUs, true);
        publish(voice, "direct_error");
    }
}
#endif

static esp_err_t takePlaybackEvents(LocalVoice *voice) {
    DSPetPlaybackEvent event;
    while (DSPetPlaybackTakeEvent(&event)) {
        if (event.kind == kDSPetPlaybackEventQueued) {
            int64_t warmupUs = kBargeWarmupUs;
#ifdef CONFIG_DSPET_DIRECT_TEST
            if (voice->direct) {
                warmupUs = kDirectBargeWarmupUs;
            }
#endif
            voice->armedUs = event.occurredUs + warmupUs;
            publish(voice, "playback_started");
            continue;
        }
        const uint32_t played = voice->playbackTurnId;
        voice->playbackTurnId = 0;
        if (voice->cancelledUs) {
            printf("DSPET_VOICE_DRAIN turn=%" PRIu32 " cancel_to_drain_us=%" PRId64 "\n",
                   played,
                   event.occurredUs - voice->cancelledUs);
            voice->cancelledUs = 0;
        }
        if (event.kind == kDSPetPlaybackEventFailed) {
#ifdef CONFIG_DSPET_DIRECT_TEST
            if (voice->direct) {
                DSPetDirectVoiceCancel(voice->direct, played);
                directFailed(voice, played, event.error);
                continue;
            }
#endif
            return event.error;
        }
        if (event.kind == kDSPetPlaybackEventFinished &&
            DSPetVoiceSessionApply(&voice->session, kDSPetVoiceReplyEnd, played)) {
            beginListening(voice, event.occurredUs, kReplyTailUs, false);
            publish(voice, "playback_finished");
        }
    }
    return ESP_OK;
}

static void markReplyReady(LocalVoice *voice, uint32_t turn) {
    voice->playbackTurnId = turn;
    voice->armedUs        = INT64_MAX;
    bool restartTimeline  = false;
#ifdef CONFIG_DSPET_DIRECT_TEST
    restartTimeline = voice->direct != NULL;
#endif
    DSPetNearSpeechReset(voice->speech, restartTimeline);
    DSPetVoiceSessionApply(&voice->session, kDSPetVoiceReplyReady, turn);
    publish(voice, "reply_ready");
}

#ifdef CONFIG_DSPET_DIRECT_TEST
static esp_err_t advanceDirect(LocalVoice *voice) {
    const uint32_t turn = voice->session.turnId;
    if (voice->submittedTurnId != turn) {
        // 上一条请求可能还在关闭 HTTPS，此时保留已结束的第二句，稍后提交。
        if (!DSPetDirectVoiceGetStatus(voice->direct).complete) {
            return ESP_OK;
        }
        size_t         samples = 0;
        size_t         preRoll;
        const int16_t *recording;
        if (voice->fixture) {
            recording = kReplyPcmStart;
            samples   = 2 * kDSPetAudioDeviceSampleRateHz;
        } else {
            recording = DSPetNearSpeechRecording(voice->speech, &samples, &preRoll);
        }
        esp_err_t error = DSPetDirectVoiceSubmit(voice->direct, turn, recording, samples, voice->newSession);
        if (error != ESP_OK) {
            return error;
        }
        voice->submittedTurnId = turn;
        voice->newSession      = false;
        voice->fixture         = false;
        publish(voice, "direct_submitted");
    }

    const DSPetDirectVoiceStatus status = DSPetDirectVoiceGetStatus(voice->direct);
    if (status.complete && status.result != ESP_OK) {
        directFailed(voice, turn, status.result);
    } else if (status.ready) {
        const esp_err_t error = DSPetPlaybackStartStream(DSPetDirectVoiceRead, voice->direct);
        if (error != ESP_OK) {
            return error;
        }
        printf("DSPET_DIRECT_STREAM turn=%" PRIu32 " before_end=%u\n", turn, status.complete ? 0u : 1u);
        markReplyReady(voice, turn);
    }
    return ESP_OK;
}

static void startFixture(LocalVoice *voice) {
    if (!voice->direct) {
        return;
    }
    if (voice->session.state == kDSPetVoiceIdle) {
        startSession(voice, "fixture_start");
        DSPetVoiceSessionApply(&voice->session, kDSPetVoiceSpeechStart, 0);
    } else if (voice->session.state == kDSPetVoiceSpeaking) {
        handleSpeech(voice, kDSPetSpeechStarted, esp_timer_get_time());
    } else {
        return;
    }
    if (DSPetVoiceSessionApply(&voice->session, kDSPetVoiceSpeechEnd, 0)) {
        voice->fixture = true;
        publish(voice, "fixture_end");
    }
}
#endif

static esp_err_t advanceSession(LocalVoice *voice, int64_t nowUs) {
#ifdef CONFIG_DSPET_DIRECT_TEST
    if (voice->direct && voice->session.state == kDSPetVoiceWaiting) {
        return voice->playbackTurnId ? ESP_OK : advanceDirect(voice);
    }
#endif
    if (voice->session.state == kDSPetVoiceWaiting && !voice->playbackTurnId && nowUs >= voice->deadlineUs) {
        // 本地回复使用固定中文；就绪和完成事件都携带发起播放时的回合编号。
        const uint32_t turn  = voice->session.turnId;
        esp_err_t      error = DSPetPlaybackStart(kReplyPcmStart, kReplyPcmEnd - kReplyPcmStart);
        if (error != ESP_OK) {
            return error;
        }
        markReplyReady(voice, turn);
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
#ifdef CONFIG_DSPET_DIRECT_TEST
    if (voice->direct && voice->session.state == kDSPetVoiceWaiting && !voice->playbackTurnId) {
        // 直连等待期只排空 DMA 观测。真正播报时再重新对齐并开始消回声。
        return ESP_OK;
    }
#endif

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
#ifdef CONFIG_DSPET_DIRECT_TEST
    if (atomic_exchange(&sFixtureRequested, false)) {
        startFixture(voice);
    }
#endif
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
    esp_err_t error = ESP_OK;
#ifdef CONFIG_DSPET_DIRECT_TEST
    if (atomic_load(&sUseDirect)) {
        error = DSPetDirectVoiceCreate(&sDirectSettings, &voice->direct);
        mbedtls_platform_zeroize(&sDirectSettings, sizeof(sDirectSettings));
    }
    voice->submittedTurnId = 0;
#endif
    if (error == ESP_OK) {
        error = DSPetWakeWordCreate(&voice->wakeWord);
    }
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
#ifdef CONFIG_DSPET_DIRECT_TEST
        if (voice->direct) {
            puts("DSPET_DIRECT_VOICE ready=1");
        }
#endif
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
#ifdef CONFIG_DSPET_DIRECT_TEST
    DSPetDirectVoiceDestroy(voice->direct);
    voice->direct = NULL;
    atomic_store(&sUseDirect, false);
    atomic_store(&sFixtureRequested, false);
#endif
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

#ifdef CONFIG_DSPET_DIRECT_TEST
esp_err_t DSPetLocalVoiceEnableDirect(const DSPetDirectSettings *settings) {
    int expected = kDSPetLocalVoiceOff;
    if (!sTask || !atomic_compare_exchange_strong(&sMode, &expected, kDSPetLocalVoiceStarting)) {
        return ESP_ERR_INVALID_STATE;
    }
    sDirectSettings = *settings;
    atomic_store(&sUseDirect, true);
    xTaskNotifyGive(sTask);
    return ESP_OK;
}

bool DSPetLocalVoiceDirectActive(void) { return atomic_load(&sUseDirect); }

esp_err_t DSPetLocalVoiceStartFixture(void) {
    const DSPetVoiceState state = DSPetLocalVoiceGetState();
    if (!DSPetLocalVoiceDirectActive() || DSPetLocalVoiceGetMode() != kDSPetLocalVoiceOn ||
        (state != kDSPetVoiceIdle && state != kDSPetVoiceSpeaking)) {
        return ESP_ERR_INVALID_STATE;
    }
    atomic_store(&sFixtureRequested, true);
    return ESP_OK;
}
#endif
