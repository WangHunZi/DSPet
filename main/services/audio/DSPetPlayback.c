#include "DSPetPlayback.h"
#include "DSPetAudioDevice.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdatomic.h>

enum { kWriteTimeoutMs = 40, kDrainTimeoutMs = 120 };

typedef struct {
    const int16_t *samples;
    size_t sampleCount;
} PlaybackRequest;

static QueueHandle_t sRequests;
static QueueHandle_t sEvents;
// 忙碌标志只由应用任务访问，取消信号由两个任务共享。
static bool sBusy;
static atomic_bool sCancelRequested;

static void publishEvent(DSPetPlaybackEventKind kind, esp_err_t error)
{
    DSPetPlaybackEvent event = {.kind = kind, .error = error, .occurredUs = esp_timer_get_time()};
    // 一个请求最多两条事件，应用读到终态后才允许提交下一条请求。
    xQueueSend(sEvents, &event, portMAX_DELAY);
}

static void playRequest(const PlaybackRequest *request, bool *deviceOpen)
{
    if (atomic_load(&sCancelRequested)) {
        publishEvent(kDSPetPlaybackEventCancelled, ESP_OK);
        return;
    }
    if (!*deviceOpen) {
        esp_err_t error = DSPetAudioDeviceOpen();
        if (error != ESP_OK) {
            publishEvent(kDSPetPlaybackEventFailed, error);
            return;
        }
        *deviceOpen = true;
    }

    esp_err_t writeResult = ESP_OK;
    bool wrote = false;
    for (size_t offset = 0; offset < request->sampleCount && !atomic_load(&sCancelRequested);) {
        size_t count = request->sampleCount - offset;
        if (count > kDSPetAudioDeviceBlockSamples) {
            count = kDSPetAudioDeviceBlockSamples;
        }
        writeResult = DSPetAudioDeviceWrite(request->samples + offset, count, kWriteTimeoutMs);
        if (writeResult != ESP_OK) {
            break;
        }
        if (!wrote) {
            publishEvent(kDSPetPlaybackEventQueued, ESP_OK);
            wrote = true;
        }
        offset += count;
    }

    // 单个写操作失败时也可能已写入部分数据。
    if (wrote || writeResult != ESP_OK) {
        esp_err_t drainResult = DSPetAudioDeviceDrain(kDrainTimeoutMs);
        if (drainResult != ESP_OK) {
            // 无法确认尾音时回收设备，下一轮再尝试打开。
            ESP_ERROR_CHECK_WITHOUT_ABORT(DSPetAudioDeviceClose());
            *deviceOpen = false;
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

static void playbackTask(void *context)
{
    bool deviceOpen = true;
    PlaybackRequest request;
    for (;;) {
        if (xQueueReceive(sRequests, &request, portMAX_DELAY) == pdTRUE) {
            playRequest(&request, &deviceOpen);
        }
    }
}

esp_err_t DSPetPlaybackInit(void)
{
    if (sRequests) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t error = DSPetAudioDeviceOpen();
    if (error != ESP_OK) {
        return error;
    }

    sRequests = xQueueCreate(1, sizeof(PlaybackRequest));
    sEvents = xQueueCreate(2, sizeof(DSPetPlaybackEvent));
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
    ESP_ERROR_CHECK_WITHOUT_ABORT(DSPetAudioDeviceClose());
    return ESP_ERR_NO_MEM;
}

esp_err_t DSPetPlaybackStart(const int16_t *samples, size_t sampleCount)
{
    if (!samples || !sampleCount) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!sRequests || sBusy) {
        return ESP_ERR_INVALID_STATE;
    }
    const PlaybackRequest request = {.samples = samples, .sampleCount = sampleCount};
    atomic_store(&sCancelRequested, false);
    if (xQueueSend(sRequests, &request, 0) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    sBusy = true;
    return ESP_OK;
}

bool DSPetPlaybackCancel(void)
{
    if (!sBusy) {
        return false;
    }
    atomic_store(&sCancelRequested, true);
    return true;
}

bool DSPetPlaybackTakeEvent(DSPetPlaybackEvent *event)
{
    if (!event || !sEvents || xQueueReceive(sEvents, event, 0) != pdTRUE) {
        return false;
    }
    if (event->kind != kDSPetPlaybackEventQueued) {
        sBusy = false;
    }
    return true;
}
