#include "DSPetPlaybackDemo.h"
#include "DSPetPlayback.h"
#include "DSPetAudioDevice.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#ifdef CONFIG_DSPET_AUDIO_DUPLEX_PROBE
#include "DSPetDuplexProbe.h"
#endif

extern const int16_t dspetPlaybackDemoStart[];
extern const int16_t dspetPlaybackDemoEnd[];

static const char *const kTag = "playback_demo";
static size_t sSampleCount;
static uint32_t sLastPresses;
static bool sPending;
static bool sDeviceOpen;
static bool sSpeaking;
static int64_t sStartedUs;
static int64_t sCancelUs;
#ifdef CONFIG_DSPET_DUPLEX_AUTO_TEST
enum { kAutoTestDone = 3 };
static unsigned sAutoTestStep;
static int64_t sAutoTestDueUs;
#endif

esp_err_t DSPetPlaybackDemoInit(void)
{
    size_t sizeBytes = (uintptr_t)dspetPlaybackDemoEnd - (uintptr_t)dspetPlaybackDemoStart;
    if (!sizeBytes || sizeBytes % sizeof(int16_t)) {
        return ESP_ERR_INVALID_SIZE;
    }
    sSampleCount = sizeBytes / sizeof(int16_t);
    esp_err_t error = DSPetAudioDeviceOpen();
    if (error != ESP_OK) {
        return error;
    }
    error = DSPetPlaybackInit();
    if (error != ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(DSPetAudioDeviceClose());
        return error;
    }
    sDeviceOpen = true;
#ifdef CONFIG_DSPET_AUDIO_DUPLEX_PROBE
    if (error == ESP_OK) {
        esp_err_t probeError = DSPetDuplexProbeStart();
        if (probeError != ESP_OK) {
            ESP_LOGW(kTag, "Duplex probe unavailable: %s", esp_err_to_name(probeError));
        }
#ifdef CONFIG_DSPET_DUPLEX_AUTO_TEST
        sAutoTestStep = probeError == ESP_OK ? 0 : kAutoTestDone;
        sAutoTestDueUs = esp_timer_get_time() + 2000000;
#endif
    }
#endif
    return error;
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
        ESP_ERROR_CHECK_WITHOUT_ABORT(DSPetAudioDeviceClose());
        sDeviceOpen = false;
        break;
    }
    sPending = false;
    sSpeaking = false;
    sCancelUs = 0;
}

static void togglePlayback(void)
{
    if (sPending) {
        if (!sCancelUs) {
            sCancelUs = esp_timer_get_time();
            DSPetPlaybackCancel();
            ESP_LOGI(kTag, "Requested stop");
        }
        return;
    }
    if (!sDeviceOpen) {
        esp_err_t openError = DSPetAudioDeviceOpen();
        if (openError != ESP_OK) {
            ESP_LOGW(kTag, "Open failed: %s", esp_err_to_name(openError));
            return;
        }
        sDeviceOpen = true;
    }
    esp_err_t error = DSPetPlaybackStart(dspetPlaybackDemoStart, sSampleCount);
    if (error != ESP_OK) {
        ESP_LOGW(kTag, "Start failed: %s", esp_err_to_name(error));
        return;
    }
    sPending = true;
    ESP_LOGI(kTag, "Requested play (%u samples)", (unsigned)sSampleCount);
}

#ifdef CONFIG_DSPET_DUPLEX_AUTO_TEST
static void advanceAutoTest(void)
{
    int64_t nowUs = esp_timer_get_time();
    if (sAutoTestStep == kAutoTestDone || nowUs < sAutoTestDueUs) {
        return;
    }
    // 第一步播放，第二步在片段中间取消，第三步等取消完成后再次播放。
    const bool needsPlayback = sAutoTestStep == 1;
    if (sPending != needsPlayback) {
        if (needsPlayback) {
            ESP_LOGW(kTag, "Automatic test stopped: playback already ended");
            sAutoTestStep = kAutoTestDone;
        }
        return;
    }
    ESP_LOGI(kTag, "Automatic test step %u/%u", sAutoTestStep + 1, kAutoTestDone);
    togglePlayback();
    sAutoTestDueUs = nowUs + (sAutoTestStep == 0 ? 3500000 : 1000000);
    ++sAutoTestStep;
}
#endif

void DSPetPlaybackDemoUpdate(uint32_t presses)
{
    DSPetPlaybackEvent event;
    while (DSPetPlaybackTakeEvent(&event)) {
        handleEvent(&event);
    }
    if (sLastPresses != presses) {
        sLastPresses = presses;
#ifdef CONFIG_DSPET_DUPLEX_AUTO_TEST
        sAutoTestStep = kAutoTestDone;
#endif
        togglePlayback();
        return;
    }
#ifdef CONFIG_DSPET_DUPLEX_AUTO_TEST
    advanceAutoTest();
#endif
}

bool DSPetPlaybackDemoSpeaking(int64_t *startedUs)
{
    if (!sSpeaking || !startedUs) {
        return false;
    }
    *startedUs = sStartedUs;
    return true;
}
