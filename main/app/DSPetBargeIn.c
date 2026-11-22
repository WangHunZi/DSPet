#include "DSPetBargeIn.h"

#include "DSPetAudioDevice.h"
#include "DSPetNearSpeech.h"
#include "DSPetPcmReplay.h"
#include "DSPetPlayback.h"
#include "esp_log.h"
#include "esp_timer.h"

enum { kTapWaitMs = 20, kMicWarmupUs = 500000, kPlaybackWarmupUs = 850000 };
static const char *const kTag = "barge_in";

static bool waitIdle(void) {
    // 只有收到播放终态后，调用方才能回收设备和源缓冲。
    DSPetPlaybackCancel();
    const int64_t deadlineUs = esp_timer_get_time() + 1000000;
    while (esp_timer_get_time() < deadlineUs) {
        DSPetPlaybackEvent event;
        if (DSPetPlaybackTakeEvent(&event) && event.kind != kDSPetPlaybackEventQueued) {
            return true;
        }
        DSPetAudioTapBlock ignored;
        DSPetAudioDeviceTakeTap(&ignored, kTapWaitMs);
    }
    return false;
}

static esp_err_t warmUp(DSPetNearSpeech *speech) {
    const int64_t deadlineUs = esp_timer_get_time() + kMicWarmupUs;
    while (esp_timer_get_time() < deadlineUs) {
        DSPetSpeechEvent   event;
        DSPetAudioTapBlock block;
        if (DSPetAudioDeviceTakeTap(&block, kTapWaitMs)) {
            esp_err_t result = DSPetNearSpeechPush(speech, &block, kDSPetNearSpeechDisarmed, &event);
            if (result != ESP_OK) {
                return result;
            }
        }
    }
    return ESP_OK;
}

static esp_err_t
listenDuringPlayback(DSPetNearSpeech *speech, size_t promptSamples, DSPetBargeInResult *result, bool *playbackIdle) {
    const int64_t deadlineUs =
        esp_timer_get_time() + (int64_t) promptSamples * 1000000 / kDSPetAudioDeviceSampleRateHz + 5000000;
    int64_t armedUs       = 0;
    int64_t cancelledUs   = 0;
    bool    announced     = false;
    bool    inputFinished = false;
    while (esp_timer_get_time() < deadlineUs) {
        DSPetPlaybackEvent playback;
        while (DSPetPlaybackTakeEvent(&playback)) {
            if (playback.kind == kDSPetPlaybackEventQueued) {
                armedUs = playback.occurredUs + kPlaybackWarmupUs;
                continue;
            }
            *playbackIdle = true;
            if (playback.kind == kDSPetPlaybackEventFailed) {
                return playback.error;
            }
            if (playback.kind == kDSPetPlaybackEventCancelled && cancelledUs) {
                result->interrupted = true;
                result->drainUs     = playback.occurredUs - cancelledUs;
                ESP_LOGI(kTag, "BARGE_IN_DRAINED");
            } else if (playback.kind == kDSPetPlaybackEventFinished && !cancelledUs) {
                result->naturalEnd = true;
                return ESP_OK;
            } else {
                return ESP_ERR_INVALID_RESPONSE;
            }
        }
        if (*playbackIdle && inputFinished) {
            return ESP_OK;
        }
        const bool armed = !*playbackIdle && !cancelledUs && armedUs && esp_timer_get_time() >= armedUs;
        if (armed && !announced) {
            announced = true;
            ESP_LOGI(kTag, "BARGE_IN_ARMED");
        }

        DSPetAudioTapBlock block;
        if (!DSPetAudioDeviceTakeTap(&block, kTapWaitMs)) {
            continue;
        }
        DSPetSpeechEvent          event;
        const DSPetNearSpeechMode mode  = armed ? kDSPetNearSpeechPlaying : kDSPetNearSpeechDisarmed;
        esp_err_t                 error = DSPetNearSpeechPush(speech, &block, mode, &event);
        if (error != ESP_OK) {
            ++result->lostBlocks;
            return error;
        }
        if (event == kDSPetSpeechStarted) {
            cancelledUs = esp_timer_get_time();
            DSPetPlaybackCancel();
            ESP_LOGI(kTag, "BARGE_IN_CANCEL");
        } else if (event == kDSPetSpeechFinished) {
            inputFinished = true;
        }
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t replayRecording(int16_t *pcm, size_t sampleCount, bool *playbackIdle) {
    DSPetPcmReplayLevel level;
    if (!DSPetPcmReplayNormalize(pcm, sampleCount, &level)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    ESP_LOGI(kTag, "BARGE_IN_REPLAY rms=%.1f gain=%.2f", level.rms, level.gain);
    esp_err_t result = DSPetPlaybackStart(pcm, sampleCount);
    if (result != ESP_OK) {
        return result;
    }
    *playbackIdle = false;
    const int64_t deadlineUs =
        esp_timer_get_time() + (int64_t) sampleCount * 1000000 / kDSPetAudioDeviceSampleRateHz + 2000000;
    while (esp_timer_get_time() < deadlineUs) {
        DSPetPlaybackEvent playback;
        if (DSPetPlaybackTakeEvent(&playback) && playback.kind != kDSPetPlaybackEventQueued) {
            *playbackIdle = true;
            return playback.kind == kDSPetPlaybackEventFinished ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
        }
        DSPetAudioTapBlock ignored;
        DSPetAudioDeviceTakeTap(&ignored, kTapWaitMs);
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t DSPetBargeInRun(const int16_t *prompt, size_t sampleCount, bool replay, DSPetBargeInResult *result) {
    if (!prompt || !sampleCount || !result) {
        return ESP_ERR_INVALID_ARG;
    }
    *result = (DSPetBargeInResult) {0};
    DSPetNearSpeech *speech;
    esp_err_t        error = DSPetNearSpeechCreate(&speech);
    if (error != ESP_OK) {
        return error;
    }

    error             = DSPetAudioDeviceOpen();
    bool deviceOpen   = error == ESP_OK;
    bool playbackIdle = true;
    if (deviceOpen) {
        error = DSPetAudioDeviceStartTap();
    }
    if (error == ESP_OK) {
        error = warmUp(speech);
    }
    if (error == ESP_OK) {
        error = DSPetPlaybackStart(prompt, sampleCount);
        if (error == ESP_OK) {
            playbackIdle = false;
            error        = listenDuringPlayback(speech, sampleCount, result, &playbackIdle);
        }
    }
    result->peakRms = DSPetNearSpeechPeakRms(speech);
    if (error == ESP_OK && result->interrupted) {
        int16_t *pcm = DSPetNearSpeechRecording(speech, &result->recordedSamples, &result->preRollSamples);
        if (!pcm) {
            error = ESP_ERR_INVALID_RESPONSE;
        } else if (replay) {
            error = replayRecording(pcm, result->recordedSamples, &playbackIdle);
        }
    }
    if (!playbackIdle) {
        playbackIdle = waitIdle();
    }
    if (playbackIdle) {
        if (deviceOpen) {
            esp_err_t closeResult = DSPetAudioDeviceClose();
            if (error == ESP_OK) {
                error = closeResult;
            }
        }
        DSPetNearSpeechDestroy(speech);
    } else {
        // 播放任务可能仍持有录音缓冲，留给复位回收。
        ESP_LOGE(kTag, "Playback task still active, reboot required");
    }
    return error;
}
