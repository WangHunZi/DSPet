#include "DSPetPlaybackDemo.h"
#include "DSPetPlayback.h"
#include "esp_log.h"
#include "esp_timer.h"

extern const int16_t dspetPlaybackDemoStart[];
extern const int16_t dspetPlaybackDemoEnd[];

static const char *const kTag = "playback_demo";
static size_t sSampleCount;
static uint32_t sLastPresses;
static bool sPending;
static bool sSpeaking;
static int64_t sStartedUs;
static int64_t sCancelUs;

esp_err_t DSPetPlaybackDemoInit(void)
{
    size_t sizeBytes = (uintptr_t)dspetPlaybackDemoEnd - (uintptr_t)dspetPlaybackDemoStart;
    if (!sizeBytes || sizeBytes % sizeof(int16_t)) {
        return ESP_ERR_INVALID_SIZE;
    }
    sSampleCount = sizeBytes / sizeof(int16_t);
    return DSPetPlaybackInit();
}

static void handleEvent(const DSPetPlaybackEvent *event)
{
    switch (event->kind) {
    case kDSPetPlaybackEventQueued:
        sSpeaking = true;
        sStartedUs = event->occurredUs;
        ESP_LOGI(kTag, "First PCM block queued");
        return;
    case kDSPetPlaybackEventFinished:
        ESP_LOGI(kTag, "Finished; DMA drained");
        break;
    case kDSPetPlaybackEventCancelled:
        ESP_LOGI(kTag, "Cancelled; DMA drained %.1f ms after request",
                 (event->occurredUs - sCancelUs) / 1000.0);
        break;
    case kDSPetPlaybackEventFailed:
        ESP_LOGW(kTag, "Playback failed: %s", esp_err_to_name(event->error));
        break;
    }
    sPending = false;
    sSpeaking = false;
    sCancelUs = 0;
}

void DSPetPlaybackDemoUpdate(uint32_t presses)
{
    DSPetPlaybackEvent event;
    while (DSPetPlaybackTakeEvent(&event)) {
        handleEvent(&event);
    }
    if (sLastPresses == presses) {
        return;
    }
    sLastPresses = presses;
    if (sPending) {
        if (!sCancelUs) {
            sCancelUs = esp_timer_get_time();
            DSPetPlaybackCancel();
            ESP_LOGI(kTag, "BOOT requested stop");
        }
        return;
    }
    esp_err_t error = DSPetPlaybackStart(dspetPlaybackDemoStart, sSampleCount);
    if (error != ESP_OK) {
        ESP_LOGW(kTag, "Start failed: %s", esp_err_to_name(error));
        return;
    }
    sPending = true;
    ESP_LOGI(kTag, "BOOT requested play (%u samples)", (unsigned)sSampleCount);
}

bool DSPetPlaybackDemoSpeaking(int64_t *startedUs)
{
    if (!sSpeaking || !startedUs) {
        return false;
    }
    *startedUs = sStartedUs;
    return true;
}
