#include "DSPetDirectVoice.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "DSPetNearSpeech.h"
#include "DSPetPcm24kTo16k.h"
#include "DSPetPcmBufferPolicy.h"
#include "DSPetPcmReplay.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "mbedtls/platform_util.h"

enum {
    kBufferBytes        = 2 * 1024 * 1024,
    kReadWaitMs         = 8,
    kWriteWaitMs        = 20,
    kDecodeBytes        = 256,
    kSpeakerAttenuation = 20,
    kTranscriptBytes    = 512,
    kReplyBytes         = 2048,
};

typedef struct {
    int16_t recording[kDSPetNearSpeechMaxRecordingSamples];
    uint8_t ring[kBufferBytes + 1];
    char    priorQuestion[kTranscriptBytes];
    char    priorReply[kReplyBytes];
    char    question[kTranscriptBytes];
    char    reply[kReplyBytes];
} VoiceBuffers;

struct DSPetDirectVoice {
    DSPetDirectSettings  settings;
    VoiceBuffers        *buffers;
    StreamBufferHandle_t audio;
    StaticStreamBuffer_t audioStorage;
    SemaphoreHandle_t    exited;
    TaskHandle_t         worker;
    atomic_bool          stopping;
    atomic_bool          cancelled;
    atomic_bool          complete;
    esp_err_t            result;
    uint32_t             turnId;
    uint32_t             priorTurnId;
    bool                 priorInterrupted;
    bool                 allowIgnore;
    size_t               samples;
    DSPetPcm24kTo16k     converter;
    size_t               produced;
    atomic_uint          gaps;
    atomic_uint          firstAudioMs;
    bool                 refilling;
    bool                 wifiOpen;
};

static bool cancelRequested(void *context) {
    DSPetDirectVoice *voice = context;
    return atomic_load(&voice->cancelled);
}

static esp_err_t enqueueAudio(const uint8_t *bytes, size_t count, void *context) {
    DSPetDirectVoice *voice = context;
    while (count) {
        int16_t      block[kDecodeBytes / 2 + 2];
        const size_t take    = count < kDecodeBytes ? count : kDecodeBytes;
        const size_t samples = DSPetPcm24kTo16kDecode(&voice->converter, bytes, take, block);
        for (size_t i = 0; i < samples; ++i) {
            block[i] /= kSpeakerAttenuation;
        }
        const size_t   length = samples * sizeof(*block);
        const uint8_t *pcm    = (const uint8_t *) block;
        for (size_t sent = 0; sent < length;) {
            if (cancelRequested(voice)) {
                return ESP_ERR_INVALID_STATE;
            }
            sent += xStreamBufferSend(voice->audio, pcm + sent, length - sent, pdMS_TO_TICKS(kWriteWaitMs));
        }
        if (!voice->produced && samples) {
            uint32_t nowMs = (uint32_t) (esp_timer_get_time() / 1000);
            atomic_store(&voice->firstAudioMs, nowMs ? nowMs : 1);
        }
        voice->produced += samples;
        bytes += take;
        count -= take;
    }
    return ESP_OK;
}

static esp_err_t runTurn(DSPetDirectVoice *voice) {
    const DSPetDeepSeekCancel cancel = {.requested = cancelRequested, .context = voice};
    if (!DSPetWifiIsConnected()) {
        esp_err_t error = DSPetWifiConnect(voice->settings.ssid, voice->settings.password);
        if (error != ESP_OK) {
            return error;
        }
    }
    VoiceBuffers       *buffer  = voice->buffers;
    const int64_t       startUs = esp_timer_get_time();
    DSPetPcmReplayLevel level;
    if (!DSPetPcmReplayNormalize(buffer->recording, voice->samples, &level)) {
        return ESP_ERR_NOT_FOUND;
    }
    // 上传的副本转为 ASR 支持的 8 kHz，用相邻两点平均抑制混叠。
    const size_t uploadSamples = voice->samples / 2;
    for (size_t i = 0; i < uploadSamples; ++i) {
        buffer->recording[i] = (int16_t) (((int32_t) buffer->recording[2 * i] + buffer->recording[2 * i + 1]) / 2);
    }
    esp_err_t error = DSPetDeepSeekTranscribe(voice->settings.key,
                                              buffer->recording,
                                              uploadSamples,
                                              8000,
                                              buffer->question,
                                              sizeof(buffer->question),
                                              &cancel);
    if (error != ESP_OK) {
        return error;
    }
    printf("DSPET_DIRECT_ASR turn=%" PRIu32 " elapsed_us=%" PRId64 " text=%s\n",
           voice->turnId,
           esp_timer_get_time() - startUs,
           buffer->question);

    const DSPetDeepSeekHistory history = {
        .question    = buffer->priorQuestion,
        .reply       = buffer->priorReply,
        .interrupted = voice->priorInterrupted,
    };
    error = DSPetDeepSeekReply(voice->settings.key,
                               buffer->question,
                               *buffer->priorReply ? &history : NULL,
                               voice->allowIgnore,
                               buffer->reply,
                               sizeof(buffer->reply),
                               &cancel);
    if (error != ESP_OK) {
        return error;
    }
    printf("DSPET_DIRECT_ANSWER turn=%" PRIu32 " text=%s\n", voice->turnId, buffer->reply);
    error = DSPetDeepSeekSpeak(voice->settings.key, buffer->reply, enqueueAudio, voice, &cancel);
    if (error == ESP_OK && (voice->converter.haveLowByte || !voice->produced)) {
        error = ESP_ERR_INVALID_RESPONSE;
    }
    if (voice->produced) {
        memcpy(buffer->priorQuestion, buffer->question, strlen(buffer->question) + 1);
        memcpy(buffer->priorReply, buffer->reply, strlen(buffer->reply) + 1);
        voice->priorTurnId      = voice->turnId;
        voice->priorInterrupted = error != ESP_OK;
    }
    return error;
}

static void workerTask(void *context) {
    DSPetDirectVoice *voice = context;
    uint8_t           discarded[512];
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (atomic_load(&voice->stopping)) {
            break;
        }
        // 旧播放器已到终态。由网络任务丢弃余样本，避免重置大缓冲时长时间持有临界区。
        while (xStreamBufferReceive(voice->audio, discarded, sizeof(discarded), 0)) {
            if (cancelRequested(voice)) {
                break;
            }
        }
        voice->result = runTurn(voice);
        printf("DSPET_DIRECT_END turn=%" PRIu32 " result=%s cancelled=%u samples=%zu gaps=%u\n",
               voice->turnId,
               esp_err_to_name(voice->result),
               cancelRequested(voice) ? 1u : 0u,
               voice->produced,
               atomic_load(&voice->gaps));
        atomic_store(&voice->complete, true);
    }
    xSemaphoreGive(voice->exited);
    vTaskDelete(NULL);
}

esp_err_t DSPetDirectVoiceCreate(const DSPetDirectSettings *settings, DSPetDirectVoice **out) {
    *out                    = NULL;
    DSPetDirectVoice *voice = heap_caps_calloc(1, sizeof(*voice), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!voice) {
        return ESP_ERR_NO_MEM;
    }
    voice->settings = *settings;
    atomic_init(&voice->complete, true);
    atomic_init(&voice->cancelled, false);
    atomic_init(&voice->stopping, false);
    atomic_init(&voice->gaps, 0);
    atomic_init(&voice->firstAudioMs, 0);
    voice->buffers  = heap_caps_calloc(1, sizeof(*voice->buffers), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    voice->exited   = xSemaphoreCreateBinary();
    esp_err_t error = voice->buffers && voice->exited ? DSPetWifiOpen() : ESP_ERR_NO_MEM;
    voice->wifiOpen = error == ESP_OK;
    if (error == ESP_OK) {
        error = DSPetWifiConnect(voice->settings.ssid, voice->settings.password);
    }
    if (error == ESP_OK) {
        if (voice->settings.unixTime) {
            const struct timeval wallTime = {.tv_sec = voice->settings.unixTime};
            error                         = settimeofday(&wallTime, NULL) == 0 ? ESP_OK : ESP_FAIL;
        } else {
            error = DSPetWifiSyncTime();
        }
    }
    if (error == ESP_OK) {
        // 容量加一个哨兵字节，确保可读写的空间一直是 PCM16 样本的整倍数。
        voice->audio = xStreamBufferCreateStatic(
            sizeof(voice->buffers->ring), sizeof(int16_t), voice->buffers->ring, &voice->audioStorage);
        if (!voice->audio ||
            xTaskCreatePinnedToCore(workerTask, "deepseek", 8192, voice, 2, &voice->worker, 0) != pdPASS) {
            error = ESP_ERR_NO_MEM;
        }
    }
    if (error != ESP_OK) {
        DSPetDirectVoiceDestroy(voice);
        return error;
    }
    *out = voice;
    return ESP_OK;
}

esp_err_t DSPetDirectVoiceSubmit(DSPetDirectVoice *voice,
                                 uint32_t          turnId,
                                 const int16_t    *samples,
                                 size_t            sampleCount,
                                 bool              newSession,
                                 bool              allowIgnore) {
    if (!atomic_load(&voice->complete)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!sampleCount || sampleCount > kDSPetNearSpeechMaxRecordingSamples) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (newSession) {
        voice->buffers->priorReply[0] = '\0';
        voice->priorTurnId            = 0;
    } else if (voice->priorTurnId == voice->turnId && atomic_load(&voice->cancelled)) {
        voice->priorInterrupted = true;
    }
    voice->turnId      = turnId;
    voice->samples     = sampleCount;
    voice->allowIgnore = allowIgnore;
    voice->converter   = (DSPetPcm24kTo16k) {0};
    voice->produced    = 0;
    memcpy(voice->buffers->recording, samples, sampleCount * sizeof(*samples));
    atomic_store(&voice->gaps, 0);
    atomic_store(&voice->firstAudioMs, 0);
    voice->refilling = false;
    atomic_store(&voice->cancelled, false);
    atomic_store(&voice->complete, false);
    xTaskNotifyGive(voice->worker);
    return ESP_OK;
}

DSPetDirectVoiceStatus DSPetDirectVoiceGetStatus(DSPetDirectVoice *voice) {
    const bool      complete  = atomic_load(&voice->complete);
    const esp_err_t result    = complete ? voice->result : ESP_OK;
    const size_t    bytes     = xStreamBufferBytesAvailable(voice->audio);
    const uint32_t  firstMs   = atomic_load(&voice->firstAudioMs);
    const uint32_t  nowMs     = (uint32_t) (esp_timer_get_time() / 1000);
    const uint32_t  elapsedMs = firstMs ? nowMs - firstMs : 0;
    return (DSPetDirectVoiceStatus) {
        .turnId   = voice->turnId,
        .complete = complete,
        .result   = result,
        .ready    = firstMs && result == ESP_OK && DSPetPcmBufferCanStart(bytes, kBufferBytes, elapsedMs, complete),
    };
}

esp_err_t DSPetDirectVoiceRead(void *context, int16_t *samples, size_t capacitySamples, size_t *sampleCount) {
    DSPetDirectVoice *voice = context;
    *sampleCount            = 0;
    if (cancelRequested(voice)) {
        return ESP_OK;
    }
    bool complete = atomic_load(&voice->complete);
    if (complete && voice->result != ESP_OK) {
        return voice->result;
    }
    if (voice->refilling &&
        !DSPetPcmBufferCanResume(xStreamBufferBytesAvailable(voice->audio), kBufferBytes, complete)) {
        vTaskDelay(pdMS_TO_TICKS(kReadWaitMs));
        return ESP_ERR_TIMEOUT;
    }
    voice->refilling = false;
    size_t bytes     = xStreamBufferReceive(
        voice->audio, samples, capacitySamples * sizeof(*samples), complete ? 0 : pdMS_TO_TICKS(kReadWaitMs));
    *sampleCount = bytes / sizeof(*samples);
    if (bytes || complete) {
        return ESP_OK;
    }
    voice->refilling = true;
    atomic_fetch_add(&voice->gaps, 1);
    return ESP_ERR_TIMEOUT;
}

void DSPetDirectVoiceCancel(DSPetDirectVoice *voice, uint32_t turnId) {
    if (voice->turnId == turnId) {
        atomic_store(&voice->cancelled, true);
    }
}

void DSPetDirectVoiceDestroy(DSPetDirectVoice *voice) {
    if (!voice) {
        return;
    }
    if (voice->worker) {
        atomic_store(&voice->stopping, true);
        atomic_store(&voice->cancelled, true);
        xTaskNotifyGive(voice->worker);
        xSemaphoreTake(voice->exited, portMAX_DELAY);
    }
    if (voice->audio) {
        vStreamBufferDelete(voice->audio);
    }
    if (voice->exited) {
        vSemaphoreDelete(voice->exited);
    }
    if (voice->wifiOpen) {
        DSPetWifiClose();
    }
    mbedtls_platform_zeroize(&voice->settings, sizeof(voice->settings));
    free(voice->buffers);
    free(voice);
}
