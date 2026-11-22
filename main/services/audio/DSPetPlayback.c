#include "DSPetPlayback.h"

#include <stdatomic.h>

#include "DSPetAudioDevice.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

enum { kWriteTimeoutMs = 40, kDrainTimeoutMs = 120 };

typedef struct {
    const int16_t    *samples;
    size_t            sampleCount;
    DSPetPlaybackRead read;
    void             *context;
} PlaybackRequest;

static QueueHandle_t sRequests;
static QueueHandle_t sEvents;
// 忙碌标志只由应用任务访问，取消信号由两个任务共享。
static bool        sBusy;
static atomic_bool sCancelRequested;

static void publishEvent(DSPetPlaybackEventKind kind, esp_err_t error) {
    DSPetPlaybackEvent event = {.kind = kind, .error = error, .occurredUs = esp_timer_get_time()};
    // 一个请求最多两条事件，应用读到终态后才允许提交下一条请求。
    xQueueSend(sEvents, &event, portMAX_DELAY);
}

static void playRequest(const PlaybackRequest *request) {
    if (atomic_load(&sCancelRequested)) {
        publishEvent(kDSPetPlaybackEventCancelled, ESP_OK);
        return;
    }
    esp_err_t writeResult    = ESP_OK;
    bool      wrote          = false;
    bool      writeAttempted = false;
    size_t    offset         = 0;
    int16_t   buffer[kDSPetAudioDeviceBlockSamples];
    while (!atomic_load(&sCancelRequested)) {
        size_t         count;
        const int16_t *samples;
        if (request->read) {
            writeResult = request->read(request->context, buffer, kDSPetAudioDeviceBlockSamples, &count);
            if (writeResult == ESP_ERR_TIMEOUT) {
                writeResult = ESP_OK;
                continue;
            }
            if (writeResult != ESP_OK || !count || atomic_load(&sCancelRequested)) {
                break;
            }
            samples = buffer;
        } else {
            count = request->sampleCount - offset;
            if (!count) {
                break;
            }
            if (count > kDSPetAudioDeviceBlockSamples) {
                count = kDSPetAudioDeviceBlockSamples;
            }
            samples = request->samples + offset;
            offset += count;
        }

        writeAttempted = true;
        writeResult    = DSPetAudioDeviceWrite(samples, count, kWriteTimeoutMs);
        if (writeResult != ESP_OK) {
            break;
        }
        if (!wrote) {
            publishEvent(kDSPetPlaybackEventQueued, ESP_OK);
            wrote = true;
        }
    }

    // 即使写操作报错，也可能已将部分样本送入 DMA。
    if (writeAttempted) {
        esp_err_t drainResult = DSPetAudioDeviceDrain(kDrainTimeoutMs);
        if (drainResult != ESP_OK) {
            publishEvent(kDSPetPlaybackEventFailed, drainResult);
            return;
        }
    }
    if (writeResult != ESP_OK) {
        publishEvent(kDSPetPlaybackEventFailed, writeResult);
    } else if (atomic_load(&sCancelRequested)) {
        publishEvent(kDSPetPlaybackEventCancelled, ESP_OK);
    } else {
        publishEvent(kDSPetPlaybackEventFinished, ESP_OK);
    }
}

static void playbackTask(void *context) {
    PlaybackRequest request;
    for (;;) {
        if (xQueueReceive(sRequests, &request, portMAX_DELAY) == pdTRUE) {
            playRequest(&request);
        }
    }
}

esp_err_t DSPetPlaybackInit(void) {
    if (sRequests) {
        return ESP_ERR_INVALID_STATE;
    }
    sRequests = xQueueCreate(1, sizeof(PlaybackRequest));
    sEvents   = xQueueCreate(2, sizeof(DSPetPlaybackEvent));
    if (sRequests && sEvents && xTaskCreate(playbackTask, "pcm-playback", 3072, NULL, 4, NULL) == pdPASS) {
        return ESP_OK;
    }
    if (sRequests) {
        vQueueDelete(sRequests);
        sRequests = NULL;
    }
    if (sEvents) {
        vQueueDelete(sEvents);
        sEvents = NULL;
    }
    return ESP_ERR_NO_MEM;
}

static esp_err_t enqueuePlayback(const PlaybackRequest *request) {
    if (!sRequests || sBusy) {
        return ESP_ERR_INVALID_STATE;
    }
    atomic_store(&sCancelRequested, false);
    if (xQueueSend(sRequests, request, 0) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    sBusy = true;
    return ESP_OK;
}

esp_err_t DSPetPlaybackStart(const int16_t *samples, size_t sampleCount) {
    if (!samples || !sampleCount) {
        return ESP_ERR_INVALID_ARG;
    }
    const PlaybackRequest request = {.samples = samples, .sampleCount = sampleCount};
    return enqueuePlayback(&request);
}

esp_err_t DSPetPlaybackStartStream(DSPetPlaybackRead read, void *context) {
    if (!read) {
        return ESP_ERR_INVALID_ARG;
    }
    const PlaybackRequest request = {.read = read, .context = context};
    return enqueuePlayback(&request);
}

bool DSPetPlaybackCancel(void) {
    if (!sBusy) {
        return false;
    }
    atomic_store(&sCancelRequested, true);
    return true;
}

bool DSPetPlaybackTakeEvent(DSPetPlaybackEvent *event) {
    if (!event || !sEvents || xQueueReceive(sEvents, event, 0) != pdTRUE) {
        return false;
    }
    if (event->kind != kDSPetPlaybackEventQueued) {
        sBusy = false;
    }
    return true;
}
